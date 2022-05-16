// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement LVM helper functions.

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::str;

use anyhow::{Context, Result};
use log::{info, warn};

use crate::hiberutil::{
    checked_command, checked_command_output, is_snapshot_active, stateful_block_partition_one,
};
use crate::mmapbuf::MmapBuffer;

/// Define the minimum size of a block device sector.
const SECTOR_SIZE: usize = 512;
/// Define the size of an LVM extent.
const LVM_EXTENT_SIZE: i64 = 64 * 1024;

/// Helper function to determine if this is a system where the stateful
/// partition is running on top of LVM.
pub fn is_lvm_system() -> Result<bool> {
    let partition1 = stateful_block_partition_one()?;
    let mut file = File::open(&partition1)?;
    let mut buffer = MmapBuffer::new(4096)?;
    let buf = buffer.u8_slice_mut();
    file.read_exact(buf)
        .context(format!("Failed to read {}", partition1))?;
    // LVM systems have a Physical Volume Label header that starts with
    // "LABELONE" as its magic. If that's found, this is an LVM system.
    // https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/4/html/cluster_logical_volume_manager/lvm_metadata
    match str::from_utf8(&buf[512..520]) {
        Ok(l) => Ok(l == "LABELONE"),
        Err(_) => Ok(false),
    }
}

/// Get the path to the given logical volume.
pub fn lv_path(volume_group: &str, name: &str) -> PathBuf {
    PathBuf::from(format!("/dev/{}/{}", volume_group, name))
}

/// Get the volume group name for the stateful block device.
pub fn get_vg_name(blockdev: &str) -> Result<String> {
    let output = checked_command_output(Command::new("/sbin/pvdisplay").args([
        "-C",
        "--noheadings",
        "-o",
        "vg_name",
        blockdev,
    ]))
    .context("Cannot run pvdisplay to get volume group name")?;

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
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

pub struct ActivatedVolumeGroup {
    vg_name: Option<String>,
}

impl ActivatedVolumeGroup {
    fn new(vg_name: String) -> Result<Self> {
        // If it already exists, don't reactivate it.
        if fs::metadata(format!("/dev/{}/unencrypted", vg_name)).is_ok() {
            return Ok(Self { vg_name: None });
        }

        checked_command(Command::new("/sbin/vgchange").args(["-ay", &vg_name]))
            .context("Cannot activate volume group")?;

        Ok(Self {
            vg_name: Some(vg_name),
        })
    }
}

impl Drop for ActivatedVolumeGroup {
    fn drop(&mut self) {
        if let Some(vg_name) = &self.vg_name {
            let r = checked_command(Command::new("/sbin/vgchange").args(["-an", vg_name]));

            match r {
                Ok(_) => {
                    info!("Deactivated vg {}", vg_name);
                }
                Err(e) => {
                    warn!("Failed to deactivate VG {}: {}", vg_name, e);
                }
            }
        }
    }
}

pub fn activate_physical_vg() -> Result<Option<ActivatedVolumeGroup>> {
    if !is_snapshot_active() {
        return Ok(None);
    }

    let partition1 = stateful_block_partition_one()?;
    // Assume that a failure to get the VG name indicates a non-LVM system.
    let vg_name = match get_vg_name(&partition1) {
        Ok(vg) => vg,
        Err(_) => {
            return Ok(None);
        }
    };

    let vg = ActivatedVolumeGroup::new(vg_name)?;
    Ok(Some(vg))
}
