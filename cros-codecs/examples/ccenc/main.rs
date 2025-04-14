// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ccenc, a simple encoder program using cros-codecs.

use std::fs::File;
use std::io::ErrorKind;
use std::io::Read;
use std::io::Write;
use std::path::PathBuf;
use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;
use std::time::Duration;

use cros_codecs::bitstream_utils::IvfFileHeader;
use cros_codecs::bitstream_utils::IvfFrameHeader;
use cros_codecs::c2_wrapper::c2_encoder::C2EncoderWorker;
#[cfg(feature = "v4l2")]
use cros_codecs::c2_wrapper::c2_v4l2_encoder::C2V4L2Encoder;
#[cfg(feature = "v4l2")]
use cros_codecs::c2_wrapper::c2_v4l2_encoder::C2V4L2EncoderOptions;
#[cfg(feature = "vaapi")]
use cros_codecs::c2_wrapper::c2_vaapi_encoder::C2VaapiEncoder;
#[cfg(feature = "vaapi")]
use cros_codecs::c2_wrapper::c2_vaapi_encoder::C2VaapiEncoderOptions;
use cros_codecs::c2_wrapper::C2EncodeJob;
use cros_codecs::c2_wrapper::C2Status;
use cros_codecs::c2_wrapper::C2Worker;
use cros_codecs::c2_wrapper::C2Wrapper;
use cros_codecs::c2_wrapper::DrainMode;
use cros_codecs::decoder::StreamInfo;
use cros_codecs::utils::align_up;
use cros_codecs::video_frame::frame_pool::FramePool;
use cros_codecs::video_frame::frame_pool::PooledVideoFrame;
use cros_codecs::video_frame::gbm_video_frame::GbmDevice;
use cros_codecs::video_frame::gbm_video_frame::GbmUsage;
use cros_codecs::video_frame::generic_dma_video_frame::GenericDmaVideoFrame;
use cros_codecs::video_frame::VideoFrame;
use cros_codecs::Fourcc;
use cros_codecs::Resolution;

mod util;

use crate::util::Args;
use crate::util::Codec;

// We use pooled video frames here because it prevents us from exhausting the FD limit on the
// machine. Unfortunately this does require us to cap the pipeline depth to the frame pool size.
const PIPELINE_DEPTH: usize = 8;

fn codec_to_fourcc(codec: &Codec) -> Fourcc {
    match codec {
        Codec::H264 => Fourcc::from(b"H264"),
        Codec::VP8 => Fourcc::from(b"VP80"),
        Codec::VP9 => Fourcc::from(b"VP90"),
        _ => panic!("Unsupported format!"),
    }
}

fn codec_to_ivf_magic(codec: &Codec) -> [u8; 4] {
    match codec {
        // Note that H264 does not generally use IVF containers.
        Codec::VP8 => IvfFileHeader::CODEC_VP8,
        Codec::VP9 => IvfFileHeader::CODEC_VP9,
        _ => panic!("Unsupported format!"),
    }
}

#[allow(clippy::too_many_arguments)]
fn enqueue_work<W>(
    encoder: &mut C2Wrapper<C2EncodeJob<PooledVideoFrame<GenericDmaVideoFrame<()>>>, W>,
    file: &mut File,
    framepool: &mut FramePool<GenericDmaVideoFrame<()>>,
    input_coded_resolution: Resolution,
    num_frames: u64,
    bitrate: u64,
    framerate: u32,
    timestamp: &mut u64,
) -> bool
where
    W: C2Worker<C2EncodeJob<PooledVideoFrame<GenericDmaVideoFrame<()>>>>,
{
    assert!(input_coded_resolution.width % 2 == 0);
    assert!(input_coded_resolution.height % 2 == 0);

    if *timestamp >= num_frames {
        if *timestamp == num_frames {
            encoder.queue(vec![C2EncodeJob {
                drain: DrainMode::EOSDrain,
                timestamp: *timestamp,
                ..Default::default()
            }]);
            *timestamp += 1;
        }
        return false;
    }

    let mut new_frame = match framepool.alloc() {
        Some(frame) => frame,
        // We've exhausted the pipeline depth
        None => return false,
    };

    {
        let visible_resolution = new_frame.resolution();
        let dst_pitches = new_frame.get_plane_pitch();
        let horizontal_subsampling = new_frame.get_horizontal_subsampling();
        let vertical_subsampling = new_frame.get_vertical_subsampling();
        let bpp = new_frame.get_bytes_per_element();
        let dst_mapping = new_frame.map_mut().expect("Failed to map input frame!");
        let dst_planes = dst_mapping.get();

        for plane in 0..dst_planes.len() {
            let bytes_per_row =
                align_up(visible_resolution.width as usize, horizontal_subsampling[plane])
                    / horizontal_subsampling[plane]
                    * (bpp[plane] as usize);
            let coded_bytes_per_row =
                align_up(input_coded_resolution.width as usize, horizontal_subsampling[plane])
                    / horizontal_subsampling[plane]
                    * (bpp[plane] as usize);
            let mut h_pad: Vec<u8> = vec![0; coded_bytes_per_row - bytes_per_row];
            let mut v_pad: Vec<u8> = vec![0; coded_bytes_per_row];
            let num_rows =
                align_up(visible_resolution.height as usize, vertical_subsampling[plane])
                    / vertical_subsampling[plane];
            let coded_num_rows =
                align_up(input_coded_resolution.height as usize, vertical_subsampling[plane])
                    / vertical_subsampling[plane];
            for y in 0..num_rows {
                let row = &mut (*dst_planes[plane].borrow_mut())
                    [(y * dst_pitches[plane])..(y * dst_pitches[plane] + bytes_per_row)];
                match file.read_exact(row) {
                    Ok(_) => (),
                    Err(e) => {
                        if e.kind() == ErrorKind::UnexpectedEof {
                            // We've reached the end of the input file, start draining.
                            encoder.queue(vec![C2EncodeJob {
                                drain: DrainMode::EOSDrain,
                                timestamp: *timestamp,
                                ..Default::default()
                            }]);
                            *timestamp = u64::MAX;
                            return false;
                        } else {
                            panic!("Error reading input file! {:?}", e);
                        }
                    }
                }
                match file.read_exact(&mut h_pad[..]) {
                    Ok(_) => (),
                    Err(e) => {
                        if e.kind() == ErrorKind::UnexpectedEof {
                            // We've reached the end of the input file, start draining.
                            encoder.queue(vec![C2EncodeJob {
                                drain: DrainMode::EOSDrain,
                                timestamp: *timestamp,
                                ..Default::default()
                            }]);
                            *timestamp = u64::MAX;
                            return false;
                        } else {
                            panic!("Error reading input file! {:?}", e);
                        }
                    }
                }
            }
            for _i in num_rows..coded_num_rows {
                match file.read_exact(&mut v_pad[..]) {
                    Ok(_) => (),
                    Err(e) => {
                        if e.kind() == ErrorKind::UnexpectedEof {
                            // We've reached the end of the input file, start draining.
                            encoder.queue(vec![C2EncodeJob {
                                drain: DrainMode::EOSDrain,
                                timestamp: *timestamp,
                                ..Default::default()
                            }]);
                            *timestamp = u64::MAX;
                            return false;
                        } else {
                            panic!("Error reading input file! {:?}", e);
                        }
                    }
                }
            }
        }
    }

    let job = C2EncodeJob {
        input: Some(new_frame),
        output: vec![],
        timestamp: *timestamp,
        bitrate,
        framerate: Arc::new(AtomicU32::new(framerate)),
        drain: DrainMode::NoDrain,
    };
    encoder.queue(vec![job]);

    *timestamp += 1;

    true
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

    let mut input = File::open(&args.input).expect("error opening input file!");

    let input_fourcc = Fourcc::from(b"NV12");
    let codec = args.codec.unwrap_or_default();
    let output_fourcc = codec_to_fourcc(&codec);

    let gbm_device = Arc::new(
        GbmDevice::open(PathBuf::from("/dev/dri/renderD128")).expect("Could not open GBM device!"),
    );
    let gbm_device_clone = gbm_device.clone();

    let aux_framepool = Arc::new(Mutex::new(FramePool::new(move |stream_info: &StreamInfo| {
        <Arc<GbmDevice> as Clone>::clone(&gbm_device)
            .new_frame(
                stream_info.format.into(),
                stream_info.display_resolution,
                stream_info.coded_resolution,
                GbmUsage::Encode,
            )
            .expect("Could not allocate frame for frame pool!")
            .to_generic_dma_video_frame()
            .expect("Could not export GBM frame to DMA frame!")
    })));

    let aux_framepool_ = aux_framepool.clone();
    let aux_framepool_hint_cb = move |stream_info: StreamInfo| {
        (*aux_framepool_.lock().unwrap()).resize(&stream_info);
    };

    let alloc_cb = move || (*aux_framepool.lock().unwrap()).alloc();

    let error_cb = move |_status: C2Status| {
        panic!("Unrecoverable encoding error!");
    };

    let output_file = Arc::new(Mutex::new(
        File::create(args.output.unwrap()).expect("Error creating output file"),
    ));
    if codec != Codec::H264 {
        let hdr = IvfFileHeader::new(
            codec_to_ivf_magic(&codec),
            args.width as u16,
            args.height as u16,
            args.framerate,
            args.count as u32,
        );
        hdr.writo_into(&mut *output_file.lock().unwrap()).expect("Error writing IVF file header!");
    }
    let is_done = Arc::new(Mutex::new(false));
    let is_done_clone = is_done.clone();
    let codec_ = codec;
    let work_done_cb = move |job: C2EncodeJob<PooledVideoFrame<GenericDmaVideoFrame<()>>>| {
        if job.drain == DrainMode::EOSDrain {
            *is_done_clone.lock().expect("Could not lock done var") = true;
        }
        if codec_ != Codec::H264 {
            let hdr =
                IvfFrameHeader { timestamp: job.timestamp, frame_size: job.output.len() as u32 };
            hdr.writo_into(&mut *output_file.lock().unwrap())
                .expect("Error writing IVF frame header!");
        }
        let _ = (*output_file.lock().unwrap())
            .write(job.output.as_slice())
            .expect("Error writing output file!");
    };

    let input_coded_resolution = Resolution {
        width: args.coded_width.unwrap_or(args.width),
        height: args.coded_height.unwrap_or(args.height),
    };
    assert!(input_coded_resolution.width >= args.width);
    assert!(input_coded_resolution.height >= args.height);

    #[cfg(feature = "v4l2")]
    let mut encoder: C2Wrapper<_, C2EncoderWorker<_, C2V4L2Encoder>> = C2Wrapper::new(
        input_fourcc,
        output_fourcc,
        error_cb,
        work_done_cb,
        aux_framepool_hint_cb,
        alloc_cb,
        C2V4L2EncoderOptions {
            output_fourcc,
            visible_resolution: Resolution { width: args.width, height: args.height },
        },
    );
    #[cfg(feature = "vaapi")]
    let mut encoder: C2Wrapper<_, C2EncoderWorker<_, C2VaapiEncoder>> = C2Wrapper::new(
        input_fourcc,
        output_fourcc,
        error_cb,
        work_done_cb,
        aux_framepool_hint_cb,
        alloc_cb,
        C2VaapiEncoderOptions {
            low_power: args.low_power,
            visible_resolution: Resolution { width: args.width, height: args.height },
        },
    );

    let mut input_framepool = FramePool::new(move |stream_info: &StreamInfo| {
        <Arc<GbmDevice> as Clone>::clone(&gbm_device_clone)
            .new_frame(
                stream_info.format.into(),
                stream_info.display_resolution,
                stream_info.coded_resolution,
                GbmUsage::Encode,
            )
            .expect("Could not allocate frame for frame pool!")
            .to_generic_dma_video_frame()
            .expect("Could not export GBM frame to DMA frame!")
    });
    input_framepool.resize(&StreamInfo {
        format: args.fourcc,
        coded_resolution: input_coded_resolution,
        display_resolution: Resolution::from((args.width, args.height)),
        min_num_frames: PIPELINE_DEPTH,
    });

    let mut timestamp: u64 = 0;

    // Start the encoder
    encoder.start();

    // Run the encode job
    while !*is_done.lock().expect("Could not lock done var") {
        // Enqueue as much as we can until the framepool is exhausted
        while enqueue_work(
            &mut encoder,
            &mut input,
            &mut input_framepool,
            input_coded_resolution,
            args.count as u64,
            args.bitrate,
            args.framerate,
            &mut timestamp,
        ) {}

        thread::sleep(Duration::from_millis(10));
    }
}
