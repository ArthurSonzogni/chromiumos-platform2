// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use log::info;

use crate::mount;

/// Identifies the target device to install onto. The device
/// is identified by having the remote install payload on it.
pub fn get_target_device() -> Result<PathBuf> {
    let disks = get_disks()?;

    for disk in disks {
        info!("Checking disk: {}", disk.display());
        if check_disk_contains_flexor(&disk) {
            info!("Found payload on {}", disk.display());
            return Ok(disk);
        }
    }

    bail!("Unable to locate disk");
}

/// Get information about all disk devices.
fn get_disks() -> Result<Vec<PathBuf>> {
    let devices = crate::lsblk::get_lsblk_devices().context("failed to get block devices")?;

    let mut disks = Vec::new();
    for device in devices {
        if device.device_type != "disk" {
            continue;
        }
        disks.push(Path::new(&device.name).into());
    }
    Ok(disks)
}

fn check_disk_contains_flexor(path: &Path) -> bool {
    let Some(flex_depl_part) = libchromeos::disk::get_partition_device(path, crate::DATA_PART_NUM) else {
        return false;
    };

    if !matches!(flex_depl_part.try_exists(), Ok(true)) {
        return false;
    };

    let Ok(flex_depl_mount) = mount::Mount::mount_by_path(flex_depl_part, mount::FsType::Vfat)
    else {
        return false;
    };

    matches!(
        flex_depl_mount
            .mount_path()
            .join("flex_image.tar.xz")
            .try_exists(),
        Ok(true)
    )
}
