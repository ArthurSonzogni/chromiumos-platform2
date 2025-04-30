// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use cros_codecs_test_common::ccdec::execution_utils::run_ccdec_test_by_codec_group;
    use cros_codecs_test_common::ccdec::verification_test_vectors::VP9_WEBM_FILES;

    #[test]
    fn profile_0_group1_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group1_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group1_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group1_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group1_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group1_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group1")
            .expect("No test vectors found for group1 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/group1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }
}
