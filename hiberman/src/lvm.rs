// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement LVM helper functions.

use std::fs::OpenOptions;
use std::io::{Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{Context, Result};

use crate::hiberutil::checked_command;

/// Define the minimum size of a block device sector.
const SECTOR_SIZE: usize = 512;
/// Define the size of an LVM extent.
const LVM_EXTENT_SIZE: i64 = 64 * 1024;

/// Get the path to the given logical volume.
pub fn lv_path(volume_group: &str, name: &str) -> PathBuf {
    PathBuf::from(format!("/dev/{}/{}", volume_group, name))
}

/// Create a new thinpool volume under the given volume group, with the
/// specified name and size.
pub fn create_thin_volume(volume_group: &str, size_mb: i64, name: &str) -> Result<()> {
    // lvcreate --thin -V "${lv_size}M" -n "{name}" "${volume_group}/thinpool"
    let size = format!("{}M", size_mb);
    let thinpool = format!("{}/thinpool", volume_group);
    checked_command(
        Command::new("/sbin/lvcreate").args(["--thin", "-V", &size, "-n", name, &thinpool]),
    )
    .context("Cannot create logical volume")
}

/// Take a newly created thin volume and ensure space is fully allocated for it
/// from the thinpool. This is destructive to the data on the volume.
pub fn thicken_thin_volume<P: AsRef<Path>>(path: P, size_mb: i64) -> Result<()> {
    let mut file = OpenOptions::new()
        .write(true)
        .open(path.as_ref())
        .context(format!(
            "Failed to open thin disk: {}",
            path.as_ref().display()
        ))?;
    let buf = [0u8; SECTOR_SIZE];
    let size_bytes = size_mb * 1024 * 1024;
    let skip = LVM_EXTENT_SIZE - (SECTOR_SIZE as i64);
    let mut offset = 0;

    loop {
        file.write_all(&buf).context("Failed to thicken LV")?;
        offset += LVM_EXTENT_SIZE;
        if offset >= size_bytes {
            break;
        }
        file.seek(SeekFrom::Current(skip)).context(format!(
            "Failed to seek {}/{} in LV",
            offset + skip,
            size_bytes
        ))?;
    }

    Ok(())
}
