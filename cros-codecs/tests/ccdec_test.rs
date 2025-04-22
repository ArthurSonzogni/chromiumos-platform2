// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::cros_codecs_decode;

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn av1_decode() {
        const AV1_DATA_PATH: &str = "src/codec/av1/test_data/test-25fps.av1.ivf";
        cros_codecs_decode(AV1_DATA_PATH, "av1");
    }

    #[test]
    fn h264_decode() {
        const H264_DATA_PATH: &str = "src/codec/h264/test_data/test-25fps.h264";
        cros_codecs_decode(H264_DATA_PATH, "h264");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn h265_decode() {
        const H265_DATA_PATH: &str = "src/codec/h265/test_data/test-25fps.h265";
        cros_codecs_decode(H265_DATA_PATH, "h265");
    }

    #[test]
    fn vp8_decode() {
        const VP8_DATA_PATH: &str = "src/codec/vp8/test_data/test-25fps.vp8";
        cros_codecs_decode(VP8_DATA_PATH, "vp8");
    }

    #[test]
    fn vp9_decode() {
        const VP9_DATA_PATH: &str = "src/codec/vp9/test_data/test-25fps.vp9";
        cros_codecs_decode(VP9_DATA_PATH, "vp9");
    }
}
