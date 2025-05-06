// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
#[allow(dead_code)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::run_ccdec_test_by_codec_group;
    use cros_codecs_test_common::ccdec::verification_test_vectors::H265_FILENAMES;
    use cros_codecs_test_common::ccdec::verification_test_vectors::H265_FILES_FROM_BUGS;

    #[test]
    fn main_part_1() {
        let filenames =
            H265_FILENAMES.get("main_part_1").expect("No test vectors found for main_part_1 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn main_part_2() {
        let filenames =
            H265_FILENAMES.get("main_part_2").expect("No test vectors found for main_part_2 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn main_part_3() {
        let filenames =
            H265_FILENAMES.get("main_part_3").expect("No test vectors found for main_part_3 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn main_part_4() {
        let filenames =
            H265_FILENAMES.get("main_part_4").expect("No test vectors found for main_part_4 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_239819547() {
        let filenames = H265_FILES_FROM_BUGS
            .get("239819547")
            .expect("No test vectors found for 239819547 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_239927523() {
        let filenames = H265_FILES_FROM_BUGS
            .get("239927523")
            .expect("No test vectors found for 239927523 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_239936640() {
        let filenames = H265_FILES_FROM_BUGS
            .get("239936640")
            .expect("No test vectors found for 239936640 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241775056() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241775056")
            .expect("No test vectors found for 241775056 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241731431() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241731431")
            .expect("No test vectors found for 241731431 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241733687() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241733687")
            .expect("No test vectors found for 241733687 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241727534() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241727534")
            .expect("No test vectors found for 241727534 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241731425() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241731425")
            .expect("No test vectors found for 241731425 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_241772308() {
        let filenames = H265_FILES_FROM_BUGS
            .get("241772308")
            .expect("No test vectors found for 241772308 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_242708185() {
        let filenames = H265_FILES_FROM_BUGS
            .get("242708185")
            .expect("No test vectors found for 242708185 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }

    #[test]
    fn bugs_251179086() {
        let filenames = H265_FILES_FROM_BUGS
            .get("251179086")
            .expect("No test vectors found for 251179086 group");
        run_ccdec_test_by_codec_group(filenames, "h265");
    }
}
