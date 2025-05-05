// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use cros_codecs_test_common::ccenc::execution_utils::cros_codecs_encode;
    use cros_codecs_test_common::ccenc::quality::Decoder;
    use cros_codecs_test_common::{Resolution, WebMFile};

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn vp9_encode() {
        let webm_file = WebMFile::new(
            "desktop2-320x180_30frames.vp9.webm", // name
            "tests/ccenc/test_data/desktop2-320x180_30frames.vp9.webm", // path
            Resolution { width: 320, height: 180 }, // size
            30,                                   // fps
            30,                                   // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }

    #[test]
    fn h264_encode() {
        let webm_file = WebMFile::new(
            "desktop2-320x180_30frames.vp9.webm", // name
            "tests/ccenc/test_data/desktop2-320x180_30frames.vp9.webm", // path
            Resolution { width: 320, height: 180 }, // size
            30,                                   // fps
            30,                                   // num_frames
        );
        cros_codecs_encode(&webm_file, "h264", Decoder::Libavc);
    }
}
