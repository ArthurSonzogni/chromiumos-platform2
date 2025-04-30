// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module provides helper functions for the ccdec_[codec]_test modules.
// It is kept separate and not exposed in a common file to avoid to having all
// test modules grouped together by test harness.

use lazy_static::lazy_static;
use std::collections::HashMap;

lazy_static! {
    /// VP8 test filenames categorized by group.
    pub static ref VP8_FILENAMES: HashMap<&'static str, Vec<&'static str>> = vp8_files();
    /// VP9 test filenames categorized by profile, group and test type.
    pub static ref VP9_WEBM_FILES: HashMap<&'static str, HashMap<&'static str, HashMap<&'static str, Vec<&'static str>>>> = vp9_webm_files();
}

#[allow(dead_code)]
/// Provides VP8 test vector files categorized by group.
pub fn vp8_files() -> HashMap<&'static str, Vec<&'static str>> {
    let mut vp8_files: HashMap<&'static str, Vec<&'static str>> = HashMap::new();
    vp8_files.insert(
        "inter_multi_coeff",
        vec![
            "test_vectors/vp8/inter_multi_coeff/vp80-03-segmentation-1408.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-03-segmentation-1409.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-03-segmentation-1410.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-03-segmentation-1413.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-04-partitions-1404.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-04-partitions-1405.ivf",
            "test_vectors/vp8/inter_multi_coeff/vp80-04-partitions-1406.ivf",
        ],
    );
    vp8_files.insert(
        "inter_segment",
        vec!["test_vectors/vp8/inter_segment/vp80-03-segmentation-1407.ivf"],
    );
    vp8_files.insert(
        "inter",
        vec![
            "test_vectors/vp8/inter/vp80-02-inter-1402.ivf",
            "test_vectors/vp8/inter/vp80-02-inter-1412.ivf",
            "test_vectors/vp8/inter/vp80-02-inter-1418.ivf",
            "test_vectors/vp8/inter/vp80-02-inter-1424.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1403.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1425.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1426.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1427.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1432.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1435.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1436.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1437.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1441.ivf",
            "test_vectors/vp8/inter/vp80-03-segmentation-1442.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1428.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1429.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1430.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1431.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1433.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1434.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1438.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1439.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1440.ivf",
            "test_vectors/vp8/inter/vp80-05-sharpness-1443.ivf",
        ],
    );
    vp8_files.insert(
        "intra_multi_coeff",
        vec!["test_vectors/vp8/intra_multi_coeff/vp80-03-segmentation-1414.ivf"],
    );
    vp8_files.insert(
        "intra_segment",
        vec!["test_vectors/vp8/intra_segment/vp80-03-segmentation-1415.ivf"],
    );
    vp8_files.insert(
        "intra",
        vec![
            "test_vectors/vp8/intra/vp80-01-intra-1400.ivf",
            "test_vectors/vp8/intra/vp80-01-intra-1411.ivf",
            "test_vectors/vp8/intra/vp80-01-intra-1416.ivf",
            "test_vectors/vp8/intra/vp80-01-intra-1417.ivf",
            "test_vectors/vp8/intra/vp80-03-segmentation-1401.ivf",
        ],
    );
    vp8_files.insert(
        "comprehensive",
        vec![
            "test_vectors/vp8/vp80-00-comprehensive-001.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-002.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-003.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-004.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-005.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-006.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-007.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-008.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-009.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-010.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-011.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-012.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-013.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-014.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-015.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-016.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-017.ivf",
            "test_vectors/vp8/vp80-00-comprehensive-018.ivf",
        ],
    );
    vp8_files
}

#[allow(dead_code)]
/// These files come from the WebM test streams and are grouped according to
/// (rounded down) level, i.e. "group1" consists of level 1 and 1.1 streams,
/// "group2" of level 2 and 2.1, etc. This helps to keep together tests with
/// similar amounts of intended behavior/expected stress on devices.
pub fn vp9_webm_files(
) -> HashMap<&'static str, HashMap<&'static str, HashMap<&'static str, Vec<&'static str>>>> {
    let mut vp9_webm_files = HashMap::new();
    // --- Profile 0 ---
    let mut profile_0_groups = HashMap::new();
    // Profile 0 - Group 1
    let mut p0_group1 = HashMap::new();
    p0_group1.insert(
        "buf",
        vec![
            "test_vectors/vp9/Profile_0_8bit/buf/crowd_run_256X144_fr15_bd8_8buf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/buf/grass_1_256X144_fr15_bd8_8buf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/buf/street1_1_256X144_fr15_bd8_8buf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/buf/crowd_run_384X192_fr30_bd8_8buf_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/buf/grass_1_384X192_fr30_bd8_8buf_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/buf/street1_1_384X192_fr30_bd8_8buf_l11.ivf",
        ],
    );
    p0_group1.insert(
        "frm_resize",
        vec![
            "test_vectors/vp9/Profile_0_8bit/frm_resize/crowd_run_256X144_fr15_bd8_frm_resize_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/frm_resize/grass_1_256X144_fr15_bd8_frm_resize_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/frm_resize/street1_1_256X144_fr15_bd8_frm_resize_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/frm_resize/crowd_run_384X192_fr30_bd8_frm_resize_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/frm_resize/grass_1_384X192_fr30_bd8_frm_resize_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/frm_resize/street1_1_384X192_fr30_bd8_frm_resize_l11.ivf",
        ],
    );
    p0_group1.insert(
        "gf_dist",
        vec![
            "test_vectors/vp9/Profile_0_8bit/gf_dist/crowd_run_256X144_fr15_bd8_gf_dist_4_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/gf_dist/grass_1_256X144_fr15_bd8_gf_dist_4_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/gf_dist/street1_1_256X144_fr15_bd8_gf_dist_4_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/gf_dist/crowd_run_384X192_fr30_bd8_gf_dist_4_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/gf_dist/grass_1_384X192_fr30_bd8_gf_dist_4_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/gf_dist/street1_1_384X192_fr30_bd8_gf_dist_4_l11.ivf",
        ],
    );
    p0_group1.insert(
        "odd_size",
        vec![
            "test_vectors/vp9/Profile_0_8bit/odd_size/crowd_run_248X144_fr15_bd8_odd_size_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/odd_size/grass_1_248X144_fr15_bd8_odd_size_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/odd_size/street1_1_248X144_fr15_bd8_odd_size_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/odd_size/crowd_run_376X184_fr30_bd8_odd_size_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/odd_size/grass_1_376X184_fr30_bd8_odd_size_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/odd_size/street1_1_376X184_fr30_bd8_odd_size_l11.ivf",
        ],
    );
    p0_group1.insert(
        "sub8x8",
        vec![
            "test_vectors/vp9/Profile_0_8bit/sub8X8/crowd_run_256X144_fr15_bd8_sub8X8_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8X8/grass_1_256X144_fr15_bd8_sub8X8_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8X8/street1_1_256X144_fr15_bd8_sub8X8_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8X8/crowd_run_384X192_fr30_bd8_sub8X8_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8X8/grass_1_384X192_fr30_bd8_sub8X8_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8X8/street1_1_384X192_fr30_bd8_sub8X8_l11.ivf",
        ],
    );
    p0_group1.insert(
        "sub8x8_sf",
        vec![
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/crowd_run_256X144_fr15_bd8_sub8x8_sf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/grass_1_256X144_fr15_bd8_sub8x8_sf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/street1_1_256X144_fr15_bd8_sub8x8_sf_l1.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/crowd_run_384X192_fr30_bd8_sub8x8_sf_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/grass_1_384X192_fr30_bd8_sub8x8_sf_l11.ivf",
            "test_vectors/vp9/Profile_0_8bit/sub8x8_sf/street1_1_384X192_fr30_bd8_sub8x8_sf_l11.ivf",
        ],
    );
    profile_0_groups.insert("group1", p0_group1);
    vp9_webm_files.insert("profile_0", profile_0_groups);
    vp9_webm_files
}
