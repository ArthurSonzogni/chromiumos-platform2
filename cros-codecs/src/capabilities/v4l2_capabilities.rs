// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::capabilities::DecoderCapability;
use crate::device::v4l2::utils::enumerate_devices;
use crate::device::v4l2::utils::find_media_device;
use crate::fourcc_for_v4l2_stateless;
use crate::v4l2r::bindings::v4l2_frmsizeenum;
use crate::v4l2r::bindings::v4l2_queryctrl;
use crate::v4l2r::bindings::v4l2_querymenu;
use crate::v4l2r::ioctl::FormatIterator;
use crate::EncodedFormat;
use crate::Fourcc;
use crate::Resolution;

use v4l2r::bindings::V4L2_CID_MPEG_VIDEO_AV1_PROFILE;
use v4l2r::bindings::V4L2_CID_MPEG_VIDEO_H264_PROFILE;
use v4l2r::bindings::V4L2_CID_MPEG_VIDEO_HEVC_PROFILE;
use v4l2r::bindings::V4L2_CID_MPEG_VIDEO_VP8_PROFILE;
use v4l2r::bindings::V4L2_CID_MPEG_VIDEO_VP9_PROFILE;
use v4l2r::device::queue::Queue;
use v4l2r::device::{Device as VideoDevice, DeviceConfig};
use v4l2r::ioctl::enum_frame_sizes;
use v4l2r::ioctl::queryctrl;
use v4l2r::ioctl::querymenu;
use v4l2r::ioctl::CtrlId;
use v4l2r::ioctl::FrmSizeTypes;
use v4l2r::ioctl::FrmSizeTypes::Discrete;
use v4l2r::ioctl::FrmSizeTypes::StepWise;
use v4l2r::ioctl::QueryCtrlFlags;
use v4l2r::ioctl::QueryMenuError;
use v4l2r::PixelFormat;
use v4l2r::QueueType;

pub fn is_supported(codec: EncodedFormat) -> Result<Option<DecoderCapability>, String> {
    let fourcc = fourcc_for_v4l2_stateless(Fourcc::from(codec))?;
    let (video_device_path, _) =
        enumerate_devices(fourcc).ok_or(String::from("Failed to enumerate devices"))?;

    let video_device_config = DeviceConfig::new().non_blocking_dqbuf();
    let video_device = VideoDevice::open(&video_device_path, video_device_config)
        .map_err(|_| String::from("Failed to open video device"))?;

    let pix_format: PixelFormat = fourcc.0.into();
    let v4l2_frame_sizes: v4l2_frmsizeenum = enum_frame_sizes(&video_device, 0, pix_format)
        .map_err(|_| String::from("Unable to query frame sizes"))?;
    let queried_size: FrmSizeTypes =
        v4l2_frame_sizes.size().ok_or("Failed to extract frame sizes")?;
    let sizes = match queried_size {
        Discrete(v4l2_frmsize_discrete) => {
            return Err(String::from("Codec not defined using stepwise sizes"))
        }
        StepWise(v4l2_frmsize_stepwise) => v4l2_frmsize_stepwise,
    };

    let ctrl_id = match codec {
        EncodedFormat::VP8 => CtrlId::new(V4L2_CID_MPEG_VIDEO_VP8_PROFILE),
        EncodedFormat::VP9 => CtrlId::new(V4L2_CID_MPEG_VIDEO_VP9_PROFILE),
        EncodedFormat::H264 => CtrlId::new(V4L2_CID_MPEG_VIDEO_H264_PROFILE),
        EncodedFormat::H265 => CtrlId::new(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE),
        EncodedFormat::AV1 => CtrlId::new(V4L2_CID_MPEG_VIDEO_AV1_PROFILE),
        _ => return Err(String::from("Unknown Codec")),
    };
    let query_ctrl: v4l2_queryctrl =
        queryctrl(&video_device, ctrl_id.unwrap(), QueryCtrlFlags::empty())
            .map_err(|_| String::from("Query Ctrl Error"))?;

    let mut profiles = vec![];
    for index in (query_ctrl.minimum as u32)..=(query_ctrl.maximum as u32) {
        let query_menu: Result<v4l2_querymenu, _> = querymenu(&video_device, query_ctrl.id, index);
        if !query_menu.is_ok() {
            continue;
        }

        let profile_index = query_menu.unwrap().index;
        profiles.push(profile_index);
    }

    Ok(Some(DecoderCapability {
        format: codec,
        min_coded_size: Resolution { width: sizes.min_width, height: sizes.min_height },
        max_coded_size: Resolution { width: sizes.max_width, height: sizes.max_height },
        profiles,
    }))
}
