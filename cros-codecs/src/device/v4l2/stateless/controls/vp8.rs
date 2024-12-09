// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use v4l2r::bindings::v4l2_ctrl_vp8_frame;
use v4l2r::bindings::V4L2_VP8_COEFF_PROB_CNT;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_EXPERIMENTAL;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_KEY_FRAME;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_SHOW_FRAME;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT;
use v4l2r::bindings::V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN;
use v4l2r::bindings::V4L2_VP8_LF_ADJ_ENABLE;
use v4l2r::bindings::V4L2_VP8_LF_DELTA_UPDATE;
use v4l2r::bindings::V4L2_VP8_LF_FILTER_TYPE_SIMPLE;
use v4l2r::bindings::V4L2_VP8_MV_PROB_CNT;
use v4l2r::bindings::V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE;
use v4l2r::bindings::V4L2_VP8_SEGMENT_FLAG_ENABLED;
use v4l2r::bindings::V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA;
use v4l2r::bindings::V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP;

use v4l2r::controls::codec::Vp8Frame;
use v4l2r::controls::SafeExtControl;

#[derive(Default)]
pub struct V4l2CtrlVp8FrameParams {
    handle: v4l2_ctrl_vp8_frame,
}

impl V4l2CtrlVp8FrameParams {
    pub fn new() -> Self {
        Default::default()
    }
}

impl From<&V4l2CtrlVp8FrameParams> for SafeExtControl<Vp8Frame> {
    fn from(decode_params: &V4l2CtrlVp8FrameParams) -> Self {
        SafeExtControl::<Vp8Frame>::from(decode_params.handle)
    }
}
