// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(test)]
#[allow(dead_code)]
mod tests {
    use cros_codecs_test_common::ccenc::execution_utils::cros_codecs_encode;
    use cros_codecs_test_common::ccenc::quality::Decoder;
    use cros_codecs_test_common::{Resolution, WebMFile};
    #[test]
    fn vp9_180() {
        let webm_file = WebMFile::new(
            "tulip2-320x180.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/tulip2-320x180.vp9.webm", // path
            Resolution { width: 320, height: 180 },                 // size
            30,                                                     // fps
            500,                                                    // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
    #[test]
    fn vp9_360() {
        let webm_file = WebMFile::new(
            "tulip2-640x360.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/tulip2-640x360.vp9.webm", // path
            Resolution { width: 640, height: 360 },                 // size
            30,                                                     // fps
            500,                                                    // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
    #[test]
    fn vp9_720() {
        let webm_file = WebMFile::new(
            "tulip2-1280x720.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/tulip2-1280x720.vp9.webm", // path
            Resolution { width: 1280, height: 720 },                 // size
            30,                                                      // fps
            500,                                                     // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
    #[test]
    fn vp9_180_meet() {
        let webm_file = WebMFile::new(
            "gipsrestat-320x180.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/gipsrestat-320x180.vp9.webm", // path
            Resolution { width: 320, height: 180 },                     // size
            50,                                                         // fps
            846,                                                        // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
    #[test]
    fn vp9_360_meet() {
        let webm_file = WebMFile::new(
            "gipsrestat-640x360.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/gipsrestat-640x360.vp9.webm", // path
            Resolution { width: 640, height: 360 },                     // size
            50,                                                         // fps
            846,                                                        // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
    #[test]
    fn vp9_720_meet() {
        let webm_file = WebMFile::new(
            "gipsrestat-1280x720.vp9.webm",                              // name
            "/data/local/tmp/test_vectors/gipsrestat-1280x720.vp9.webm", // path
            Resolution { width: 1280, height: 720 },                     // size
            50,                                                          // fps
            846,                                                         // num_frames
        );
        cros_codecs_encode(&webm_file, "vp9", Decoder::Libvpx);
    }
}
