// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for getting information about disks/block devices/etc.

use anyhow::Result;
use std::path::PathBuf;
use std::process::Command;

use crate::platform::Platform;
use crate::process_util::{self, ProcessError};

/// Find device path for the disk containing the root filesystem.
///
/// The return value is a path in /dev, for example "/dev/sda".
// TODO(378875141): Use the rootdev library instead.
pub fn get_root_disk_device_path() -> Result<PathBuf> {
    let mut command = Command::new("rootdev");
    command.args(["-s", "-d"]);
    let output = process_util::get_output_as_string(command)?;
    let output = output.trim();
    Ok(PathBuf::from(output))
}

/// Run the factory_ufs provision command, if present. UFS is Universal
/// Flash Storage.
pub fn init_ufs(platform: &dyn Platform) -> Result<(), ProcessError> {
    let exe = platform.root().join("usr/sbin/factory_ufs");
    if !exe.exists() {
        return Ok(());
    }

    let mut cmd = Command::new(exe);
    cmd.arg("provision");
    platform.run_command(cmd)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::platform::MockPlatform;
    use std::fs;

    /// Test that `init_ufs` succeeds if `factory_ufs` does not exist.
    #[test]
    fn test_init_ufs_noop() {
        let tmpdir = tempfile::tempdir().unwrap();
        let tmpdir = tmpdir.path();

        let mut platform = MockPlatform::new();
        platform.expect_root_path(tmpdir);

        init_ufs(&platform).unwrap();
    }

    /// Test that `init_ufs` runs `factory_ufs` if it exists.
    #[test]
    fn test_init_ufs_run() {
        let tmpdir = tempfile::tempdir().unwrap();
        let tmpdir = tmpdir.path();
        let sbin = tmpdir.join("usr/sbin");
        fs::create_dir_all(&sbin).unwrap();
        let factory_ufs = sbin.join("factory_ufs");
        fs::write(&factory_ufs, "").unwrap();

        let mut platform = MockPlatform::new();
        platform.expect_root_path(tmpdir);
        platform
            .expect_run_command()
            .withf(move |cmd| {
                cmd.get_program() == factory_ufs
                    && cmd.get_args().collect::<Vec<_>>() == ["provision"]
            })
            .return_once(|_| Ok(()));

        init_ufs(&platform).unwrap();
    }
}
