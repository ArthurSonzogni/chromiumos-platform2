// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::Write;
use std::os::unix::prelude::FileExt;
use std::sync::Arc;

use cros_codecs::backend::v4l2::encoder::find_device_with_capture;
use cros_codecs::backend::v4l2::encoder::v4l2_format_to_frame_layout;
use cros_codecs::backend::v4l2::encoder::EncoderCodec;
use cros_codecs::backend::v4l2::encoder::MmapingCapture;
use cros_codecs::backend::v4l2::encoder::OutputBuffer;
use cros_codecs::backend::v4l2::encoder::OutputBufferHandle;
use cros_codecs::backend::v4l2::encoder::V4L2Backend;
use cros_codecs::bitstream_utils::IvfFileHeader;
use cros_codecs::bitstream_utils::IvfFrameHeader;
use cros_codecs::encoder::simple_encode_loop;
use cros_codecs::encoder::stateful::h264::v4l2::V4L2StatefulH264Encoder;
use cros_codecs::encoder::stateful::h265::v4l2::V4L2StatefulH265Encoder;
use cros_codecs::encoder::stateful::vp8::v4l2::V4L2StatefulVP8Encoder;
use cros_codecs::encoder::stateful::vp9::v4l2::V4L2StatefulVP9Encoder;
use cros_codecs::encoder::stateful::StatefulEncoder;
use cros_codecs::encoder::CodedBitstreamBuffer;
use cros_codecs::encoder::FrameMetadata;
use cros_codecs::encoder::Tunings;
use cros_codecs::Fourcc;
use cros_codecs::FrameLayout;
use cros_codecs::Resolution;

use v4l2r::device::Device;
use v4l2r::device::DeviceConfig;
use v4l2r::memory::MmapHandle;

use crate::util::Args;
use crate::util::Codec;

// "Handle" abstraction for this particular use case. All Encoders take a "Handle" generic type
// that implements the OutputBufferHandle trait, which basically just tells the Encoder how the
// frame data is going to be loaded into the V4L2 output buffers. This is where we add the code to
// load the frames from the disk. Technically we could do the disk load in DiskFrameReader and
// then pass regular u8 buffers to |queue()|, but this way avoids a copy.
struct MmapNM12Frame<'a> {
    resolution: Resolution,
    file: &'a File,
    pos: u64,
}

impl OutputBufferHandle for MmapNM12Frame<'_> {
    type PrimitiveBufferHandles = Vec<MmapHandle>;

    fn queue(self, buffer: OutputBuffer<'_, Self::PrimitiveBufferHandles>) -> anyhow::Result<()> {
        // TODO(b:378856977) : This assumes the input file is NV12 frames. This is actually
        // somewhat uncommon though, as FFMPEG defaults to I420, and all of existing tast tests
        // use I420. So we should add format conversion support.
        let y_plane_size = (self.resolution.width * self.resolution.height) as usize;
        let uv_plane_size = (self.resolution.width * self.resolution.height / 2) as usize;
        let mut y_plane = buffer.get_plane_mapping(0).unwrap();
        let mut uv_plane = buffer.get_plane_mapping(1).unwrap();
        // Use |read_at()| instead of |read()| so we don't need to take a mutable reference to the
        // File. We don't know how many in flight OutputBufferHandles will be created in advance,
        // and we can only mutably borrow once. We could get around this with an Rc RefCell or an
        // Arc if we decide we need to use |read()| because we want to support non-POSIX platforms.
        assert_eq!(
            self.file
                .read_at(&mut y_plane.as_mut()[0..y_plane_size], self.pos)
                .expect("Unexpected EOF!"),
            y_plane_size
        );
        assert_eq!(
            self.file
                .read_at(
                    &mut uv_plane.as_mut()[0..uv_plane_size],
                    self.pos + y_plane_size as u64
                )
                .expect("Unexpected EOF!"),
            uv_plane_size
        );
        buffer.queue(&[y_plane.len(), uv_plane.len()])?;
        Ok(())
    }
}

// Generator for MmapNM12Frames. Note that we do the actual loading from disk in |queue()|, this
// just basically just keeps track of offsets.
struct DiskFrameReader<'a> {
    file: &'a File,
    width: u32,
    height: u32,
    layout: FrameLayout,
    pos: u64,
    frame_num: usize,
    total_frames: usize,
}

impl<'a> Iterator for DiskFrameReader<'a> {
    type Item = (FrameMetadata, MmapNM12Frame<'a>);

    fn next(&mut self) -> Option<Self::Item> {
        if self.frame_num >= self.total_frames {
            return None;
        }

        let resolution = Resolution {
            width: self.width,
            height: self.height,
        };
        let meta = FrameMetadata {
            timestamp: self.frame_num as u64,
            layout: self.layout.clone(),
            force_keyframe: false,
        };

        let handle = MmapNM12Frame {
            resolution: resolution,
            file: &self.file,
            pos: self.pos,
        };

        self.frame_num += 1;
        // TODO(b:378856977): This assumes coded size and visible size are the same.
        self.pos += (self.width * self.height * 3 / 2) as u64;

        Some((meta, handle))
    }
}

impl<'a> DiskFrameReader<'_> {
    pub fn new(
        file: &'a File,
        width: u32,
        height: u32,
        layout: FrameLayout,
        total_frames: usize,
    ) -> DiskFrameReader<'a> {
        DiskFrameReader {
            file: file,
            width: width,
            height: height,
            layout: layout,
            pos: 0,
            frame_num: 0,
            total_frames: total_frames,
        }
    }
}

// V4L2 stateful decoders are all of the form "StatefulEncoder<Handle, V4L2Backend<Handle,
// CaptureBufferz, Codec>>". Since we know that all the encoders in this file are going to be V4L2
// stateful and we know the Handle type is going to be MmapNM12Frame, we can alias this type a
// little bit to make the signature smaller.
type MmapEncoder<'a, Codec> =
    StatefulEncoder<MmapNM12Frame<'a>, V4L2Backend<MmapNM12Frame<'a>, MmapingCapture, Codec>>;

fn codec_to_pixelformat(codec: Codec) -> v4l2r::PixelFormat {
    match codec {
        Codec::H264 => v4l2r::PixelFormat::from_fourcc(b"H264"),
        Codec::H265 => v4l2r::PixelFormat::from_fourcc(b"HEVC"),
        Codec::VP9 => v4l2r::PixelFormat::from_fourcc(b"VP90"),
        Codec::VP8 => v4l2r::PixelFormat::from_fourcc(b"VP80"),
        _ => panic!("Unsupported format!"),
    }
}

fn codec_to_ivf_magic(codec: Codec) -> [u8; 4] {
    match codec {
        // Note that H264 does not generally use IVF containers.
        Codec::VP8 => IvfFileHeader::CODEC_VP8,
        Codec::VP9 => IvfFileHeader::CODEC_VP9,
        _ => panic!("Unsupported format!"),
    }
}

fn do_encode_loop<'a, Codecz>(
    mut encoder: MmapEncoder<'a, Codecz>,
    input: &'a File,
    args: Args,
) -> ()
where
    V4L2Backend<MmapNM12Frame<'a>, MmapingCapture, Codecz>: EncoderCodec,
{
    let fps = args.framerate.unwrap_or(30);

    let layout = v4l2_format_to_frame_layout(&encoder.backend().output_format().unwrap());
    let mut frame_reader =
        DiskFrameReader::new(&input, args.width, args.height, layout, args.count);

    let codec = args.codec.unwrap_or_default();
    let output_file = args.output.map(|path| {
        let mut output = File::create(path).expect("Error opening output file!");

        if codec != Codec::H264 {
            let hdr = IvfFileHeader::new(
                codec_to_ivf_magic(codec),
                args.width as u16,
                args.height as u16,
                fps,
                args.count as u32,
            );
            hdr.writo_into(&mut output)
                .expect("Error writing IVF file header!");
        }

        output
    });

    // Unwrapping an optional takes ownership of it, so we do that outside of the lambda so we
    // don't violate FnMut's lifetime requirements.
    match output_file {
        Some(mut output_file) => {
            let frame_consumer = |coded_chunk: CodedBitstreamBuffer| {
                if codec != Codec::H264 {
                    let hdr = IvfFrameHeader {
                        timestamp: coded_chunk.metadata.timestamp,
                        frame_size: coded_chunk.bitstream.len() as u32,
                    };
                    hdr.writo_into(&mut output_file)
                        .expect("Error writing IVF frame header!");
                }

                let _ = output_file
                    .write(&coded_chunk.bitstream[..])
                    .expect("Error writing output file!");
            };
            simple_encode_loop(&mut encoder, &mut frame_reader, frame_consumer)
                .expect("Failed to encode!");
        }
        None => {
            simple_encode_loop(&mut encoder, &mut frame_reader, |_| ()).expect("Failed to encode!")
        }
    };
}

pub fn do_encode(input: File, args: Args) -> () {
    let codec = args.codec.unwrap_or_default();
    let device = find_device_with_capture(codec_to_pixelformat(codec))
        .expect("Could not find an encoder for codec");
    let device = Device::open(&device, DeviceConfig::new().non_blocking_dqbuf()).expect("open");
    let device = Arc::new(device);

    let resolution = Resolution {
        width: args.width,
        height: args.height,
    };
    let fourcc = Fourcc::from(b"NM12");
    let tunings: Tunings = Default::default();

    match codec {
        Codec::H264 => do_encode_loop(
            V4L2StatefulH264Encoder::new(
                device,
                MmapingCapture,
                cros_codecs::encoder::h264::EncoderConfig {
                    resolution: resolution.clone(),
                    ..Default::default()
                },
                fourcc,
                resolution,
                tunings,
            )
            .expect("Failed to create encoder"),
            &input,
            args,
        ),
        Codec::H265 => do_encode_loop(
            V4L2StatefulH265Encoder::new(
                device,
                MmapingCapture,
                cros_codecs::encoder::h265::EncoderConfig {
                    resolution: resolution.clone(),
                    ..Default::default()
                },
                fourcc,
                resolution,
                tunings,
            )
            .expect("Failed to create encoder"),
            &input,
            args,
        ),
        Codec::VP8 => do_encode_loop(
            V4L2StatefulVP8Encoder::new(
                device,
                MmapingCapture,
                cros_codecs::encoder::vp8::EncoderConfig {
                    resolution: resolution.clone(),
                    ..Default::default()
                },
                fourcc,
                resolution,
                tunings,
            )
            .expect("Failed to create encoder"),
            &input,
            args,
        ),
        Codec::VP9 => do_encode_loop(
            V4L2StatefulVP9Encoder::new(
                device,
                MmapingCapture,
                cros_codecs::encoder::vp9::EncoderConfig {
                    resolution: resolution.clone(),
                    ..Default::default()
                },
                fourcc,
                resolution,
                tunings,
            )
            .expect("Failed to create encoder"),
            &input,
            args,
        ),
        _ => panic!("Unsupported format!"),
    };
}
