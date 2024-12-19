// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for getting information about disks/block devices/etc.

use anyhow::Result;
use std::path::PathBuf;
use std::process::Command;

use crate::process_util;

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
