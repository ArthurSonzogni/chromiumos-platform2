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

use std::path::{Path, PathBuf};
use thiserror::Error;

use crate::ccenc::execution_utils;

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
    #[error("Invalid YUV file size: {yuv_file_size:?} with frame size: {yuv_frame_size:?}")]
    InvalidYuvFileSize { yuv_file_size: u64, yuv_frame_size: u64 },
    #[error("Unsupported output format: {0}")]
    UnsupportedOutputFormat(String),
}

/// A frame resolution in pixels.
#[allow(missing_docs)]
#[derive(Clone, Copy)]
pub struct Resolution {
    pub width: u64,
    pub height: u64,
}

/// Represents metadata associated with a WebM test file.
#[allow(dead_code)]
pub struct WebMFile {
    /// The full path to the WebM file.
    pub path: PathBuf,
    /// The base name of the file.
    pub name: String,
    /// The video resolution (width and height).
    pub size: Resolution,
    /// The frames per second of the video.
    pub fps: usize,
    /// The total number of frames in the video.
    pub num_frames: usize,
}

impl WebMFile {
    /// Creates a new WebMFile while resolving the input path.
    pub fn new(name: &str, path: &str, size: Resolution, fps: usize, num_frames: usize) -> Self {
        let resolved_path = Self::resolve_path(path);
        Self { name: name.to_string(), path: resolved_path, size, fps, num_frames }
    }

    /// Resolves the given webm file path, making it absolute if necessary.
    fn resolve_path(webm_file_path: &str) -> PathBuf {
        let path = Path::new(webm_file_path);
        let resolved_path = if path.is_absolute() {
            path.to_path_buf()
        } else {
            execution_utils::get_parent_path().join(path)
        };
        if !resolved_path.exists() {
            panic!(
                "Invalid WebM file provided: {:?} (original path: {:?})",
                resolved_path, webm_file_path,
            );
        }
        resolved_path
    }
}
