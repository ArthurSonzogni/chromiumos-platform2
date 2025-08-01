// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::run_ccdec_test_by_codec_group;
    use cros_codecs_test_common::ccdec::verification_test_vectors::VP8_FILENAMES;

    #[test]
    fn inter_multi_coeff() {
        let filenames = VP8_FILENAMES
            .get("inter_multi_coeff")
            .expect("No test vectors found for inter_multi_coeff group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn inter_segment() {
        let filenames = VP8_FILENAMES
            .get("inter_segment")
            .expect("No test vectors found for inter_segment group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn inter() {
        let filenames = VP8_FILENAMES.get("inter").expect("No test vectors found for inter group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn intra_multi_coeff() {
        let filenames = VP8_FILENAMES
            .get("intra_multi_coeff")
            .expect("No test vectors found for intra_multi_coeff group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn intra_segment() {
        let filenames = VP8_FILENAMES
            .get("intra_segment")
            .expect("No test vectors found for intra_segment group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn intra() {
        let filenames = VP8_FILENAMES.get("intra").expect("No test vectors found for intra group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }

    #[test]
    fn comprehensive() {
        let filenames = VP8_FILENAMES
            .get("comprehensive")
            .expect("No test vectors found for comprehensive group");
        run_ccdec_test_by_codec_group(filenames, "vp8");
    }
}
