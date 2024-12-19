// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::process_util;
use anyhow::Result;
use std::process::Command;

/// Platform abstraction layer.
#[cfg_attr(test, mockall::automock)]
pub trait Platform {
    /// Run a command and get its stdout as a `String`.
    ///
    /// Stderr is also captured, but not returned to the caller.
    ///
    /// An error is returned if the process fails to launch or exits
    /// non-zero, or if the output is not valid utf8.
    fn run_command_and_get_stdout(&self, cmd: Command) -> Result<String>;
}

/// Non-test implementation of `Platform`.
pub struct PlatformImpl;

impl Platform for PlatformImpl {
    fn run_command_and_get_stdout(&self, cmd: Command) -> Result<String> {
        process_util::get_output_as_string(cmd)
    }
}
