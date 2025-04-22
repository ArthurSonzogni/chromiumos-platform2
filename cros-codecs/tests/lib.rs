// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Crate for common functionalities to run integration tests in cros-codecs.

/// The [ccdec] module contains tools for decoding tests with ccdec.
pub mod ccdec {
    /// Utility functions to execute decoding tests.
    pub mod execution_utils;
    /// Provides bitstreams to test correctness of video decoder implementations.
    pub mod verification_test_vectors;
}
