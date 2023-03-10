// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements LVM helper functions.

use std::fs::File;
use std::fs::OpenOptions;
use std::fs::{self};
use std::io::Read;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;
use std::process::Command;
use std::str;

use anyhow::Context;
use anyhow::Result;
use log::info;
use log::warn;

use crate::hiberutil::checked_command;
use crate::hiberutil::checked_command_output;
use crate::hiberutil::is_snapshot_active;
use crate::hiberutil::stateful_block_partition_one;
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

/// Determine if the given logical volume exists.
pub fn lv_exists(volume_group: &str, name: &str) -> Result<bool> {
    let volume = full_lv_name(volume_group, name);
    let output = Command::new("/sbin/lvdisplay")
        .arg(&volume)
        .output()
        .context("Failed to get output for child process")?;
    Ok(output.status.success())
}

/// Enumerate all activated logical volumes in the system.
pub fn get_active_lvs() -> Result<Vec<String>> {
    let output = checked_command_output(Command::new("/sbin/lvdisplay").args([
        "-C",
        "--options=name",
        "--noheadings",
    ]))
    .context("Failed to get active LVs")?;
    let output_string = String::from_utf8_lossy(&output.stdout);
    let mut elements: Vec<String> = vec![];
    output_string.split_whitespace().for_each(|e| {
        elements.push(e.trim().to_string());
    });

    Ok(elements)
}

/// Activate a logical volume.
pub fn activate_lv(volume_group: &str, name: &str) -> Result<()> {
    if lv_path(volume_group, name).exists() {
        // LV is already active
        return Ok(());
    }

    let full_name = full_lv_name(volume_group, name);
    checked_command(Command::new("/sbin/lvchange").args(["-ay", &full_name]))
        .context("Failed to activate logical volume '{full_name}'")?;

    Ok(())
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

/// Get the fully qualified name of an LV.
fn full_lv_name(volume_group: &str, name: &str) -> String {
    format!("{}/{}", volume_group, name)
}

pub struct ActivatedLogicalVolume {
    lv_arg: Option<String>,
}

impl ActivatedLogicalVolume {
    pub fn new(vg_name: &str, lv_name: &str) -> Result<Option<Self>> {
        // If it already exists, don't reactivate it.
        if fs::metadata(lv_path(vg_name, lv_name)).is_ok() {
            return Ok(None);
        }

        activate_lv(vg_name, lv_name)?;

        Ok(Some(Self {
            lv_arg: Some(full_lv_name(vg_name, lv_name)),
        }))
    }

    /// Don't deactivate the logical volume on drop.
    pub fn dont_deactivate(&mut self) {
        self.lv_arg = None;
    }
}

impl Drop for ActivatedLogicalVolume {
    fn drop(&mut self) {
        if let Some(lv_arg) = self.lv_arg.take() {
            let r = checked_command(Command::new("/sbin/lvchange").args(["-an", &lv_arg]));

            match r {
                Ok(_) => {
                    info!("Deactivated LV {}", lv_arg);
                }
                Err(e) => {
                    warn!("Failed to deactivate LV {}: {}", lv_arg, e);
                }
            }
        }
    }
}

pub fn activate_physical_lv(lv_name: &str) -> Result<Option<ActivatedLogicalVolume>> {
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

    ActivatedLogicalVolume::new(&vg_name, lv_name)
}
