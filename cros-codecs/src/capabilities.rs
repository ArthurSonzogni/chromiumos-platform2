// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::EncodedFormat;
use crate::Resolution;

#[cfg(feature = "v4l2")]
pub mod v4l2_capabilities;
#[cfg(feature = "v4l2")]
use crate::capabilities::v4l2_capabilities::is_supported;

#[cfg(feature = "vaapi")]
pub mod vaapi_capabilities;
#[cfg(feature = "vaapi")]
use crate::capabilities::vaapi_capabilities::is_supported;

// A structure that holds the capabilities of the video decoder device for a given codec.
#[derive(Debug)]
pub struct DecoderCapability {
    format: EncodedFormat,
    min_coded_size: Resolution,
    max_coded_size: Resolution,
    profiles: Vec<u32>,
}

// Returns a list of DecoderCapability structs that illustrate the capabilities
// of the video decoder device.
pub fn get_decoder_capabilities() -> Result<Vec<DecoderCapability>, String> {
    const POSSIBLE_CODECS: [EncodedFormat; 5] = [
        EncodedFormat::VP8,
        EncodedFormat::VP9,
        EncodedFormat::H264,
        EncodedFormat::H265,
        EncodedFormat::AV1,
    ];

    let mut caps = vec![];
    for codec in POSSIBLE_CODECS {
        match is_supported(codec)? {
            Some(capability) => caps.push(capability),
            None => continue,
        }
    }

    Ok(caps)
}
