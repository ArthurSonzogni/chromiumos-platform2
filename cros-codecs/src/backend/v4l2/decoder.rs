// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ColorPrimaries;
use crate::ColorRange;
#[cfg(feature = "v4l2")]
use crate::DecodedFormat;
use crate::MatrixCoefficients;
use crate::Rect;
use crate::Resolution;
use crate::TransferFunction;

use std::sync::Arc;
use v4l2r::device::Device as VideoDevice;

pub const ADDITIONAL_REFERENCE_FRAME_BUFFER: usize = 4;

pub mod stateless;

pub trait V4l2StreamInfo {
    /// Returns the minimum number of surfaces required to decode the stream.
    // name was chosen to match vaapi
    fn min_num_frames(&self) -> usize;
    /// Returns the coded size of the surfaces required to decode the stream.
    fn coded_size(&self) -> Resolution;
    /// Returns the visible rectangle within the coded size for the stream.
    fn visible_rect(&self) -> Rect;
    // Returns the bit depth for the stream.
    fn bit_depth(&self) -> usize;
    // Returns the queried DecodedFormat for the current stream.
    fn get_decoded_format(&self, device: Arc<VideoDevice>) -> Result<DecodedFormat, String>;
    // The following are used for exposing color aspect information for the stream.
    fn range(&self) -> ColorRange;
    fn primaries(&self) -> ColorPrimaries;
    fn transfer(&self) -> TransferFunction;
    fn matrix(&self) -> MatrixCoefficients;
}
