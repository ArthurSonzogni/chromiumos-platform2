// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ccdec, a simple decoder program using cros-codecs. Capable of computing MD5 checksums from the
//! input and writing the raw decoded frames to a file.

use std::borrow::Cow;
use std::fs::File;
use std::io::Read;
use std::io::Write;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Condvar;
use std::sync::Mutex;

use std::sync::atomic::{AtomicU64, Ordering};

use cros_codecs::bitstream_utils::parse_matroska;
use cros_codecs::bitstream_utils::IvfIterator;
use cros_codecs::bitstream_utils::NalIterator;
use cros_codecs::c2_wrapper::c2_decoder::C2DecoderWorker;
#[cfg(feature = "v4l2")]
use cros_codecs::c2_wrapper::c2_v4l2_decoder::C2V4L2Decoder;
#[cfg(feature = "v4l2")]
use cros_codecs::c2_wrapper::c2_v4l2_decoder::C2V4L2DecoderOptions;
#[cfg(feature = "vaapi")]
use cros_codecs::c2_wrapper::c2_vaapi_decoder::C2VaapiDecoder;
#[cfg(feature = "vaapi")]
use cros_codecs::c2_wrapper::c2_vaapi_decoder::C2VaapiDecoderOptions;
use cros_codecs::c2_wrapper::C2DecodeJob;
use cros_codecs::c2_wrapper::C2Status;
use cros_codecs::c2_wrapper::C2Wrapper;
use cros_codecs::c2_wrapper::DrainMode;
use cros_codecs::codec::h264::parser::Nalu as H264Nalu;
use cros_codecs::codec::h265::parser::Nalu as H265Nalu;
use cros_codecs::decoder::StreamInfo;
use cros_codecs::utils::align_up;
use cros_codecs::video_frame::frame_pool::FramePool;
use cros_codecs::video_frame::frame_pool::PooledVideoFrame;
use cros_codecs::video_frame::gbm_video_frame::GbmDevice;
use cros_codecs::video_frame::gbm_video_frame::GbmUsage;
use cros_codecs::video_frame::generic_dma_video_frame::GenericDmaVideoFrame;
use cros_codecs::video_frame::VideoFrame;
use cros_codecs::EncodedFormat;
use cros_codecs::Fourcc;

use crate::md5::md5_digest;
use crate::md5::MD5Context;
use crate::util::decide_output_file_name;
use crate::util::golden_md5s;
use crate::util::Args;
use crate::util::Md5Computation;

mod md5;
mod util;

// Returns the frame iterator for IVF or MKV file.
fn create_vpx_frame_iterator(input: &[u8]) -> Box<dyn Iterator<Item = Cow<[u8]>> + '_> {
    if let Ok(ivf_iterator) = IvfIterator::new(input) {
        Box::new(ivf_iterator.map(Cow::Borrowed))
    } else {
        Box::new(parse_matroska(input).into_iter().map(Cow::Borrowed))
    }
}

fn main() {
    #[cfg(feature = "android")]
    android_logger::init_once(
        android_logger::Config::default()
            .with_max_level(log::LevelFilter::Trace)
            .with_tag("cros-codecs"),
    );
    #[cfg(not(feature = "android"))]
    env_logger::init();

    let args: Args = argh::from_env();

    let mut input = File::open(&args.input).expect("error opening input file");

    let input = {
        let mut buf = Vec::new();
        input.read_to_end(&mut buf).expect("error reading input file");
        buf
    };

    let mut output = if !args.multiple_output_files {
        args.output.as_ref().map(|p| File::create(p).expect("error creating output file"))
    } else {
        None
    };

    let golden_iter = Arc::new(Mutex::new(golden_md5s(&args.golden).into_iter()));

    let frame_iter =
        match args.input_format {
            EncodedFormat::H264 => Box::new(NalIterator::<H264Nalu>::new(&input))
                as Box<dyn Iterator<Item = Cow<[u8]>>>,
            EncodedFormat::H265 => Box::new(NalIterator::<H265Nalu>::new(&input))
                as Box<dyn Iterator<Item = Cow<[u8]>>>,
            _ => create_vpx_frame_iterator(&input),
        };

    let mut _md5_context = Arc::new(Mutex::new(MD5Context::new()));
    let md5_context = _md5_context.clone();

    let mut output_filename_idx = 0;
    let need_per_frame_md5 = match args.compute_md5 {
        Some(Md5Computation::Frame) => true,
        _ => args.golden.is_some(),
    };
    let stream_mode = Some(Md5Computation::Stream) == args.compute_md5;

    let gbm_device = Arc::new(
        GbmDevice::open(PathBuf::from("/dev/dri/renderD128")).expect("Could not open GBM device!"),
    );
    let framepool = Arc::new(Mutex::new(FramePool::new(move |stream_info: &StreamInfo| {
        <Arc<GbmDevice> as Clone>::clone(&gbm_device)
            .new_frame(
                Fourcc::from(args.output_format),
                stream_info.display_resolution,
                stream_info.coded_resolution,
                GbmUsage::Decode,
            )
            .expect("Could not allocate frame for frame pool!")
            .to_generic_dma_video_frame()
            .expect("Could not export GBM frame to DMA frame!")
    })));
    // This is a workaround to get "copy by clone" semantics for closure variable capture since
    // Rust lacks the appropriate syntax to express this concept.
    let _framepool = framepool.clone();
    let framepool_hint_cb = move |stream_info: StreamInfo| {
        (*_framepool.lock().unwrap()).resize(&stream_info);
    };
    let alloc_cb = move || (*framepool.lock().unwrap()).alloc();

    // This is a condition variable that allows us to block until the we receive a notification
    // that the decoder has reached the end-of-stream.
    let eos_cv_pair = Arc::new((Mutex::new(/*received_eos=*/ false), Condvar::new()));
    let _eos_cv_pair = Arc::clone(&eos_cv_pair);

    let frames_needed = Arc::new(AtomicU64::new((*golden_iter.lock().unwrap()).len() as u64));

    let _frames_needed = frames_needed.clone();
    let on_new_frame = move |job: C2DecodeJob<PooledVideoFrame<GenericDmaVideoFrame<()>>>| {
        if args.output.is_none() && args.compute_md5.is_none() && args.golden.is_none() {
            return;
        }

        if job.output.is_some() {
            let horizontal_subsampling = job.output.as_ref().unwrap().get_horizontal_subsampling();
            let vertical_subsampling = job.output.as_ref().unwrap().get_vertical_subsampling();
            let bpp = job.output.as_ref().unwrap().get_bytes_per_element();
            let pitches = job.output.as_ref().unwrap().get_plane_pitch();
            let width = job.output.as_ref().unwrap().resolution().width as usize;
            let height = job.output.as_ref().unwrap().resolution().height as usize;
            let mut frame_data: Vec<u8> = vec![];
            {
                let frame_map =
                    job.output.as_ref().unwrap().map().expect("failed to map output frame!");
                let planes = frame_map.get();
                for plane_idx in 0..planes.len() {
                    let plane_height = align_up(height, vertical_subsampling[plane_idx])
                        / vertical_subsampling[plane_idx];
                    let plane_width = ((align_up(width, horizontal_subsampling[plane_idx])
                        / horizontal_subsampling[plane_idx])
                        as f32
                        * bpp[plane_idx]) as usize;
                    let plane = planes[plane_idx];
                    for y in 0..plane_height {
                        frame_data.extend_from_slice(
                            &plane
                                [(y * pitches[plane_idx])..(y * pitches[plane_idx] + plane_width)],
                        );
                    }
                }
            }

            if args.multiple_output_files {
                let file_name = decide_output_file_name(
                    args.output.as_ref().expect("multiple_output_files need output to be set"),
                    output_filename_idx,
                );

                let mut output = File::create(file_name).expect("error creating output file");
                output_filename_idx += 1;
                output.write_all(&frame_data).expect("failed to write to output file");
            } else if let Some(output) = &mut output {
                output.write_all(&frame_data).expect("failed to write to output file");
            }

            let frame_md5: String =
                if need_per_frame_md5 { md5_digest(&frame_data) } else { "".to_string() };

            match args.compute_md5 {
                None => (),
                Some(Md5Computation::Frame) => println!("{}", frame_md5),
                Some(Md5Computation::Stream) => (*md5_context.lock().unwrap()).consume(&frame_data),
            }

            if args.golden.is_some() {
                assert_eq!(frame_md5, (*golden_iter.lock().unwrap()).next().unwrap());
                (*_frames_needed).fetch_sub(1, Ordering::SeqCst);
            }
        }

        if job.drain == DrainMode::EOSDrain {
            // Indicates an empty "drain" signal.
            let (lock, eos_cv) = &*_eos_cv_pair;
            let mut received_eos = lock.lock().unwrap();
            *received_eos = true;
            eos_cv.notify_one();
        }
    };

    let error_cb = move |_status: C2Status| {
        panic!("Unrecoverable decoding error!");
    };

    #[cfg(feature = "vaapi")]
    let mut decoder: C2Wrapper<_, C2DecoderWorker<_, C2VaapiDecoder>> = C2Wrapper::new(
        Fourcc::from(args.input_format),
        Fourcc::from(args.output_format),
        error_cb,
        on_new_frame,
        framepool_hint_cb,
        alloc_cb,
        C2VaapiDecoderOptions { libva_device_path: args.libva_device },
    );
    #[cfg(feature = "v4l2")]
    let mut decoder: C2Wrapper<_, C2DecoderWorker<_, C2V4L2Decoder>> = C2Wrapper::new(
        Fourcc::from(args.input_format),
        Fourcc::from(args.output_format),
        error_cb,
        on_new_frame,
        framepool_hint_cb,
        alloc_cb,
        C2V4L2DecoderOptions { video_device_path: None },
    );
    let _ = decoder.start();

    let mut timestamp = 0;
    for input_frame in frame_iter {
        decoder.queue(vec![C2DecodeJob {
            input: input_frame.as_ref().to_vec(),
            timestamp: timestamp as u64,
            ..Default::default()
        }]);
        timestamp += 1;
    }
    // Manually construct a "drain" job to ensure we get an EOS marked C2DecodeJob in our callback.
    // We use this to determine when it is safe to shut down the decoder.
    decoder.queue(vec![C2DecodeJob {
        timestamp: timestamp as u64,
        drain: DrainMode::EOSDrain,
        ..Default::default()
    }]);

    // Block until the decoder reaches the end-of-stream.
    let (lock, eos_cv) = &*eos_cv_pair;
    let mut received_eos = lock.lock().unwrap();
    while !*received_eos {
        received_eos = eos_cv.wait(received_eos).unwrap();
    }

    assert!((*frames_needed).load(Ordering::SeqCst) == 0, "Not all frames were output.");

    if stream_mode {
        println!("{}", (*_md5_context.lock().unwrap()).flush());
    }
}
