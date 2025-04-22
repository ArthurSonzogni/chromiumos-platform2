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

/// The [ccenc] module contains tools for encoding tests with ccenc.
pub mod ccenc {
    /// Provides utility functions to execute encoding tests.
    pub mod execution_utils;
    /// Provides tools to collect video quality metrics.
    pub mod quality;
    /// Provides utilities for handling YUV.
    pub mod yuv;
}

use std::path::Path;
use thiserror::Error;

/// Errors during encoding tests.
#[allow(missing_docs)]
#[derive(Debug, Error)]
pub enum EncodeTestError {
    #[error("Invalid WebM file provided: {0}")]
    InvalidWebMFile(String),
    #[error("I/O Error: {0}")]
    IoError(String),
    #[error("Command {command}, failed with error: {stderr}")]
    CommandExecutionError { command: String, stderr: String },
    #[error("Failed to parse output from processing file '{file:?}': {details}")]
    ParseError { file: std::path::PathBuf, details: String },
}

/// A frame resolution in pixels.
#[allow(missing_docs)]
#[derive(Clone, Copy)]
pub struct Resolution {
    pub width: u32,
    pub height: u32,
}

/// Represents metadata associated with a WebM test file.
#[allow(dead_code)]
pub struct WebMFile<'a> {
    /// The full path to the WebM file.
    pub path: &'a Path,
    /// The base name of the file.
    pub name: &'a str,
    /// The video resolution (width and height).
    pub size: Resolution,
    /// The frames per second of the video.
    pub fps: usize,
    /// The total number of frames in the video.
    pub num_frames: usize,
}
