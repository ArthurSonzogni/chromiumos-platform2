// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module to parse soul config files from the system.
//!
//! # Example
//! ```rust
//! let config = config::read().expect("valid config");
//! open_default_file(config.default_file_name);
//! ```
mod config;
mod facility;
mod log_file;
mod program;
mod reader;

pub use crate::config::config::Config;
pub use crate::config::facility::Facility;
pub use crate::config::log_file::LogFile;
pub use crate::config::program::Program;
pub use crate::config::reader::read;
