// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module provides helper functions for the ccdec_[codec]_test modules.
// It is kept separate and not exposed in a common file to avoid to having all
// test modules grouped together by test harness.

use lazy_static::lazy_static;
use std::collections::HashMap;

lazy_static! {
    /// A HashMap lazily initialized to contain VP8 test filenames categorized by group.
    pub static ref VP8_FILENAMES: HashMap<&'static str, Vec<&'static str>> = vp8_files();
}

/// Provides a collection of VP8 test vector files categorized by test type.
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
