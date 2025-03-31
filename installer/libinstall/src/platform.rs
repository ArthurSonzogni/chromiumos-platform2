// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::process_util::{self, ProcessError};
use anyhow::{Context, Result};
use nix::mount::{umount2, MntFlags};
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

/// Platform abstraction layer.
#[cfg_attr(feature = "test_util", mockall::automock)]
pub trait Platform {
    /// Get the filesystem root.
    ///
    /// The non-test implementation returns `/`.
    fn root(&self) -> PathBuf;

    /// Run a `Command`.
    ///
    /// The command is logged before running it.
    ///
    /// Stdout and stderr are not redirected.
    ///
    /// An error is returned if the process fails to launch or exits
    /// non-zero.
    fn run_command(&self, cmd: Command) -> Result<(), ProcessError>;

    /// Run a command and get both stdout and stderr.
    ///
    /// An error is returned if the process fails to launch or exits
    /// non-zero.
    fn run_command_and_get_output(&self, cmd: Command) -> Result<Output, ProcessError>;

    /// Run a command and get its stdout as a `String`.
    ///
    /// Stderr is also captured, but not returned to the caller.
    ///
    /// An error is returned if the process fails to launch or exits
    /// non-zero, or if the output is not valid utf8.
    fn run_command_and_get_stdout(&self, cmd: Command) -> Result<String>;

    /// Unmount `target`.
    ///
    /// The non-test implementation uses the `umount2` syscall. If an
    /// error occurs, the `target` and `flags` are included in the error
    /// context.
    fn unmount(&self, target: &Path, flags: MntFlags) -> Result<()>;
}

/// Non-test implementation of `Platform`.
pub struct PlatformImpl;

impl Platform for PlatformImpl {
    fn root(&self) -> PathBuf {
        PathBuf::from("/")
    }

    fn run_command(&self, cmd: Command) -> Result<(), ProcessError> {
        process_util::log_and_run_command(cmd)
    }

    fn run_command_and_get_output(&self, cmd: Command) -> Result<Output, ProcessError> {
        process_util::get_command_output(cmd)
    }

    fn run_command_and_get_stdout(&self, cmd: Command) -> Result<String> {
        process_util::get_output_as_string(cmd)
    }

    fn unmount(&self, target: &Path, flags: MntFlags) -> Result<()> {
        umount2(target, flags)
            .with_context(|| format!("failed to unmount {} (flags={:?})", target.display(), flags))
    }
}

#[cfg(feature = "test_util")]
impl MockPlatform {
    pub fn expect_root_path(&mut self, root: &Path) {
        let root = root.to_owned();
        self.expect_root().returning(move || root.clone());
    }
}
