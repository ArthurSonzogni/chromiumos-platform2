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

    #[test]
    fn profile_0_group2_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group2_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group2_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group2_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group2_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group2_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group2")
            .expect("No test vectors found for group2 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/group2");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group3_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group3_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group3_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group3_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group3_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group3_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group3")
            .expect("No test vectors found for group3 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/group3");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group4_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group4_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group4_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group4_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_group4_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_group4_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("group4")
            .expect("No test vectors found for group4 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/group4");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_0_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_0_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_0_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_0_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_level5_0_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_0_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_0")
            .expect("No test vectors found for level5_0 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/level5_0");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_1_buf() {
        let filenames: &Vec<&'static str> = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("buf")
            .expect("No test vectors found for buf in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_1_frm_resize() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("frm_resize")
            .expect("No test vectors found for frm_resize in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_1_gf_dist() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("gf_dist")
            .expect("No test vectors found for gf_dist in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_1_odd_size() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("odd_size")
            .expect("No test vectors found for odd_size in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    fn profile_0_level5_1_sub8x8() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("sub8x8")
            .expect("No test vectors found for sub8x8 in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn profile_0_level5_1_sub8x8_sf() {
        let filenames = VP9_WEBM_FILES
            .get("profile_0")
            .expect("No test vectors found for profile_0")
            .get("level5_1")
            .expect("No test vectors found for level5_1 in profile_0")
            .get("sub8x8_sf")
            .expect("No test vectors found for sub8x8_sf in profile_0/level5_1");
        run_ccdec_test_by_codec_group(filenames, "vp9");
    }

    // TODO(400788075): Add tests with files from bugs
}
