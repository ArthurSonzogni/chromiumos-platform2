// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
#[allow(dead_code)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::run_ccdec_test_by_codec_group;
    use cros_codecs_test_common::ccdec::verification_test_vectors::H264_FILENAMES;

    #[test]
    fn baseline() {
        let filenames =
            H264_FILENAMES.get("baseline").expect("No test vectors found for baseline group");
        run_ccdec_test_by_codec_group(filenames, "h264");
    }

    #[test]
    fn main() {
        let filenames = H264_FILENAMES.get("main").expect("No test vectors found for main group");
        run_ccdec_test_by_codec_group(filenames, "h264");
    }

    #[test]
    fn first_mb_in_slice() {
        let filenames = H264_FILENAMES
            .get("first_mb_in_slice")
            .expect("No test vectors found for first_mb_in_slice group");
        run_ccdec_test_by_codec_group(filenames, "h264");
    }

    #[test]
    fn high() {
        let filenames = H264_FILENAMES.get("high").expect("No test vectors found for high group");
        run_ccdec_test_by_codec_group(filenames, "h264");
    }
}
