// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;
use std::sync::Arc;

use v4l2r::device::Device as VideoDevice;
use v4l2r::device::DeviceConfig;
use v4l2r::QueueType;

use crate::c2_wrapper::c2_decoder::C2DecoderBackend;
use crate::decoder::stateless::av1::Av1;
use crate::decoder::stateless::h264::H264;
use crate::decoder::stateless::h265::H265;
use crate::decoder::stateless::vp8::Vp8;
use crate::decoder::stateless::vp9::Vp9;
use crate::decoder::stateless::DynStatelessVideoDecoder;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessVideoDecoder;
use crate::decoder::BlockingMode;
use crate::device::v4l2::utils::enumerate_devices;
use crate::fourcc_for_v4l2_stateless;
use crate::v4l2r::ioctl::FormatIterator;
use crate::video_frame::VideoFrame;
use crate::EncodedFormat;
use crate::Fourcc;

#[derive(Clone, Debug)]
pub struct C2V4L2DecoderOptions {
    // TODO: This is currently unused, but we should plumb it to V4L2Device initialization.
    pub video_device_path: Option<PathBuf>,
}

pub struct C2V4L2Decoder {}

impl C2DecoderBackend for C2V4L2Decoder {
    type DecoderOptions = C2V4L2DecoderOptions;

    fn new(_options: C2V4L2DecoderOptions) -> Result<Self, String> {
        Ok(Self {})
    }

    fn supported_output_formats(&self, fourcc: Fourcc) -> Result<Vec<Fourcc>, String> {
        // TODO(bchoobineh): Update to support HEVC when we support V4L2 HEVC stateless decoding.
        // TODO(bchoobineh): Update logic to support 10 bit streams.
        let devices = enumerate_devices(fourcc_for_v4l2_stateless(fourcc)?);
        let (video_device_path, _) = match devices {
            Some(paths) => paths,
            None => return Err(String::from("Failed to enumerate devices")),
        };

        let video_device_config = DeviceConfig::new().non_blocking_dqbuf();
        let video_device = Arc::new(
            VideoDevice::open(&video_device_path, video_device_config)
                .map_err(|_| String::from("Failed to open video device"))?,
        );

        let supported_formats: Vec<Fourcc> =
            FormatIterator::new(&video_device, QueueType::VideoCaptureMplane)
                .map(|x| Fourcc(x.pixelformat.into()))
                .collect();

        log::info!("Supported Output Formats: {:?}", supported_formats);

        Ok(supported_formats)
    }

    fn modifier(&self) -> u64 {
        // TODO: There should be some way to dynamically query this?
        0
    }

    fn get_decoder<V: VideoFrame + 'static>(
        &mut self,
        format: EncodedFormat,
    ) -> Result<DynStatelessVideoDecoder<V>, String> {
        Ok(match format {
            EncodedFormat::AV1 => StatelessDecoder::<Av1, _>::new_v4l2(BlockingMode::NonBlocking)
                .map_err(|_| "Failed to instantiate AV1 decoder")?
                .into_trait_object(),
            EncodedFormat::H264 => StatelessDecoder::<H264, _>::new_v4l2(BlockingMode::NonBlocking)
                .map_err(|_| "Failed to instantiate H264 decoder")?
                .into_trait_object(),
            EncodedFormat::H265 => StatelessDecoder::<H265, _>::new_v4l2(BlockingMode::NonBlocking)
                .map_err(|_| "Failed to instantiate H265 decoder")?
                .into_trait_object(),
            EncodedFormat::VP8 => StatelessDecoder::<Vp8, _>::new_v4l2(BlockingMode::NonBlocking)
                .map_err(|_| "Failed to instantiate VP8 decoder")?
                .into_trait_object(),
            EncodedFormat::VP9 => StatelessDecoder::<Vp9, _>::new_v4l2(BlockingMode::NonBlocking)
                .map_err(|_| "Failed to instantiate VP9 decoder")?
                .into_trait_object(),
        })
    }
}
