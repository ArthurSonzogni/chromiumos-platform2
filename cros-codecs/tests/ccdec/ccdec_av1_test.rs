// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
#[allow(dead_code)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::run_ccdec_test_by_codec_group;
    use cros_codecs_test_common::ccdec::verification_test_vectors::AV1_AOM_8BIT_FILES;
    use cros_codecs_test_common::ccdec::verification_test_vectors::AV1_FILES;

    #[test]
    fn av1_8bit() {
        let filenames = AV1_FILES.get("8bit").expect("No test vectors found for 8bit group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }

    #[test]
    fn aom_8bit_quantizer() {
        let filenames =
            AV1_AOM_8BIT_FILES.get("quantizer").expect("No test vectors found for quantizer group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }

    #[test]
    fn aom_8bit_size() {
        let filenames =
            AV1_AOM_8BIT_FILES.get("size").expect("No test vectors found for size group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }

    #[test]
    fn aom_8bit_allintra() {
        let filenames =
            AV1_AOM_8BIT_FILES.get("allintra").expect("No test vectors found for allintra group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }

    #[test]
    fn aom_8bit_cdfupdate() {
        let filenames =
            AV1_AOM_8BIT_FILES.get("cdfupdate").expect("No test vectors found for cdfupdate group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }

    #[test]
    fn aom_8bit_motionvec() {
        let filenames =
            AV1_AOM_8BIT_FILES.get("motionvec").expect("No test vectors found for motionvec group");
        run_ccdec_test_by_codec_group(filenames, "av1");
    }
}
