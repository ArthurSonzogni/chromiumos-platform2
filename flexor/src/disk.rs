// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use gpt_disk_types::{BlockSize, GptPartitionEntry, Lba, LbaRangeInclusive};
use libchromeos::mount::{self, FsType};
use log::info;
use std::fs::File;
use std::process::Command;

use crate::cgpt;
use crate::gpt;
use crate::lsblk::LsBlkDevice;

use crate::util::execute_command;

/// Constants for the newly created FLEX_DEPLOY partition.
pub const FLEX_DEPLOY_PART_NUM_BLOCKS: u64 = 9_000_000_000 / 512;
pub const FLEX_DEPLOY_PART_LABEL: &str = "FLEX_DEPLOY";
pub const FLEX_DEPLOY_PART_NUM: u32 = 13;

pub const STATEFUL_PARTITION_LABEL: &str = "STATE";
pub const STATEFUL_PARTITION_NUM: u32 = 1;

/// Holds information about the disk Flexor installs to.
pub struct DiskInfo {
    pub target_device: PathBuf,
    pub install_partition: PathBuf,
}

/// Fetches information about the disk we want to install to. The target disk
/// is identified by having the installation payload on one of the partitions
/// (the install partition).
pub fn disk_info() -> Result<DiskInfo> {
    let device = get_disks()?;

    for device in device {
        info!("Checking device: {}", device.name);
        if let Some(disk_info) = get_install_disk_info(device) {
            info!(
                "Installing to disk: {}, reading installation payload from: {}",
                disk_info.target_device.display(),
                disk_info.install_partition.display()
            );
            return Ok(disk_info);
        }
    }

    bail!("Unable to locate payload on any disk");
}

/// Checks if one of a disk's partitions contains the installation payload.
fn get_install_disk_info(disk: LsBlkDevice) -> Option<DiskInfo> {
    fn has_install_payload(device: &Path) -> bool {
        let Ok(mount) = mount::Builder::new(device)
            .fs_type(FsType::Vfat)
            .temp_backed_mount()
        else {
            return false;
        };
        matches!(
            mount
                .mount_path()
                .join(crate::FLEX_IMAGE_FILENAME)
                .try_exists(),
            Ok(true)
        )
    }

    // We only check disks.
    if disk.device_type != "disk" {
        return None;
    }

    for children in disk.children.unwrap_or_default() {
        if children.device_type == "part" && has_install_payload(Path::new(&children.name)) {
            return Some(DiskInfo {
                target_device: PathBuf::from(&disk.name),
                install_partition: PathBuf::from(&children.name),
            });
        }
    }
    None
}

/// Reload the partition table on block devices.
pub fn reload_partitions(disk_path: &Path) -> Result<()> {
    // In some cases, we may be racing with udev for access to the
    // device leading to EBUSY when we reread the partition table.  We
    // avoid the conflict by using `udevadm settle`, so that udev goes
    // first.
    let mut settle_cmd = Command::new("udevadm");
    settle_cmd.arg("settle");
    execute_command(settle_cmd)?;

    // Now we re-read the partition table using `blockdev`.
    let mut blockdev_cmd = Command::new("/sbin/blockdev");
    blockdev_cmd.arg("--rereadpt").arg(disk_path);

    execute_command(blockdev_cmd)
}

/// Creates an EXT4 filesystem on `device`.
pub fn mkfs_ext4(disk_path: &Path) -> Result<()> {
    // We use the mkfs.ext4 binary to put the filesystem.
    let mut cmd = Command::new("mkfs.ext4");
    cmd.arg(disk_path);

    execute_command(cmd)
}

/// Inserts a thirtheenth partition after the stateful partition (shrinks
/// stateful partition). This can only be called with a disk that already
/// has a ChromeOS partition layout. Since this method is just changing
/// the partition layout but not the filesystem, it assumes the filesystem
/// on stateful partition will be re-created later.
pub fn insert_thirteenth_partition(disk_path: &Path) -> Result<()> {
    let file = File::open(disk_path)?;
    let mut gpt = gpt::Gpt::from_file(file, BlockSize::BS_512).with_context(|| {
        format!(
            "Unable to read the GPT partition table of {}",
            disk_path.display()
        )
    })?;

    let new_part_size_lba = FLEX_DEPLOY_PART_NUM_BLOCKS;

    let current_stateful = gpt
        .get_entry_for_partition_with_label(STATEFUL_PARTITION_LABEL.parse().unwrap())
        .context("Unable to locate stateful partition on disk")?;

    let new_stateful_range = shrink_partition_by(current_stateful, new_part_size_lba)
        .context("Unable to shrink stateful partiton")?;
    cgpt::resize_cgpt_partition(
        STATEFUL_PARTITION_NUM,
        disk_path,
        STATEFUL_PARTITION_LABEL,
        new_stateful_range,
    )?;

    let new_range = add_partition_after(new_stateful_range, new_part_size_lba)
        .context("Unable to calculate new partition range")?;
    cgpt::add_cgpt_partition(
        FLEX_DEPLOY_PART_NUM,
        disk_path,
        FLEX_DEPLOY_PART_LABEL,
        new_range,
    )
}

/// Removes the thirteenth partition from the disk in two steps:
/// 1. Since we've inserted the partition *after* the stateful partition, we can just
///    remove it and then grow the stateful partition back to its initial size.
/// 2. Then we grow the partition's filesystem (ext4) to its maximum size.
///
/// Please note: This should never be called while either the stateful or the flex
/// deployment partition is mounted.
pub fn try_remove_thirteenth_partition(disk_path: &Path) -> Result<()> {
    let file = File::open(disk_path)?;
    let mut gpt = gpt::Gpt::from_file(file, BlockSize::BS_512).with_context(|| {
        format!(
            "Unable to read the GPT partition table of {}",
            disk_path.display()
        )
    })?;

    // First make sure both the stateful and flex deployment partition exist.
    let stateful_part = gpt
        .get_entry_for_partition_with_label(STATEFUL_PARTITION_LABEL.parse().unwrap())
        .context("Unable to locate stateful partition on disk")?;
    let flex_dep_part = gpt
        .get_entry_for_partition_with_label(FLEX_DEPLOY_PART_LABEL.parse().unwrap())
        .context("Unable to locate flex deployment partition on disk")?;

    // Then calculate the new range and close the disk file handle.
    let new_stateful_range = merge_partition_ranges(
        stateful_part
            .lba_range()
            .context("Illegal stateful partition range detected")?,
        flex_dep_part
            .lba_range()
            .context("Illegal flex deployment partition range detected")?,
    )
    .context("Error calculating a range for a grown stateful partition")?;
    drop(gpt);

    // Then remove the flex deployment partition.
    cgpt::remove_cgpt_partition(FLEX_DEPLOY_PART_NUM, disk_path)?;
    reload_partitions(disk_path)?;

    // Now grow the stateful partition.
    cgpt::resize_cgpt_partition(
        STATEFUL_PARTITION_NUM,
        disk_path,
        STATEFUL_PARTITION_LABEL,
        new_stateful_range,
    )
    .context(
        "Unable to grow the stateful partition after removing the flex deployment partition",
    )?;
    reload_partitions(disk_path)?;

    // Finally grow the filesystem.
    extend_ext_filesystem(disk_path, STATEFUL_PARTITION_NUM)
        .context("Unable to extend the stateful partition's filesystem")
}

fn shrink_partition_by(
    part_info: GptPartitionEntry,
    size_in_lba: u64,
) -> Result<LbaRangeInclusive> {
    let current_part_size = part_info
        .lba_range()
        .context("Unable to get LbaRange")?
        .num_blocks();
    if current_part_size < size_in_lba {
        bail!(
            "Can't make place for a new partition with size {size_in_lba} if the current
             partition only has size {current_part_size}"
        );
    }

    let curr_part_new_size = current_part_size - size_in_lba;
    // Subtract one from the size because the range is "inclusive".
    let new_range = LbaRangeInclusive::new(
        Lba(part_info.starting_lba.to_u64()),
        Lba(part_info.starting_lba.to_u64() + curr_part_new_size - 1),
    )
    .context("Error calculating partition range")?;

    Ok(new_range)
}

fn merge_partition_ranges(
    first_part_range: LbaRangeInclusive,
    second_part_range: LbaRangeInclusive,
) -> Result<LbaRangeInclusive> {
    // First figure out which one is the first.
    let (lower_range, higher_range) = if first_part_range.start() < second_part_range.start() {
        (first_part_range, second_part_range)
    } else {
        (second_part_range, first_part_range)
    };

    if lower_range.end() >= higher_range.start() {
        bail!("Error while trying to merge overlapping partitions");
    }

    // Then merge. We are sure this is legal.
    Ok(LbaRangeInclusive::new(lower_range.start(), higher_range.end()).unwrap())
}

fn extend_ext_filesystem(disk_path: &Path, part_to_grow: u32) -> Result<()> {
    // Executes `e2fsck` for checking partition health.
    fn execute_e2fsck(partition_path: &Path) -> Result<()> {
        let mut cmd = Command::new("e2fsck");
        // Automate all steps that can be done without human intervention.
        // If e2fsck fails now, it is something serious.
        cmd.arg("-p");
        // Pass in the path to the partition we want to check.
        cmd.arg("-f").arg(partition_path);
        execute_command(cmd)
    }

    let partition_path = libchromeos::disk::get_partition_device(disk_path, part_to_grow)
        .context("Unable to find partition to extend")?;

    // First check the partition.
    execute_e2fsck(&partition_path)?;

    // Then actually grow the filesystem.
    let mut cmd = Command::new("resize2fs");
    cmd.arg(&partition_path);
    execute_command(cmd)?;

    // Finally check again if we were successful.
    execute_e2fsck(&partition_path)
}

fn add_partition_after(range: LbaRangeInclusive, size_in_lba: u64) -> Result<LbaRangeInclusive> {
    let new_part_range = LbaRangeInclusive::new(
        Lba(range.end().to_u64() + 1),
        Lba(range.end().to_u64() + size_in_lba),
    )
    .context("Error calculating partition range")?;

    Ok(new_part_range)
}

/// Get information about all disk devices.
fn get_disks() -> Result<Vec<LsBlkDevice>> {
    Ok(crate::lsblk::get_lsblk_devices()
        .context("Unable to get block devices")?
        .into_iter()
        .filter(|device| device.device_type == "disk")
        .collect())
}

#[cfg(test)]
mod tests {
    use crate::disk::merge_partition_ranges;

    use super::add_partition_after;
    use super::shrink_partition_by;
    use gpt_disk_types::LbaRangeInclusive;
    use gpt_disk_types::{GptPartitionEntry, Lba, LbaLe};

    #[test]
    fn test_insert_partition() {
        const STATE_START_LBA: u64 = 2;
        const STATE_END_LBA: u64 = 5;
        const NEW_PART_SIZE_BLOCKS: u64 = 3;

        let mock_stateful_info = GptPartitionEntry {
            starting_lba: LbaLe::from_u64(STATE_START_LBA),
            ending_lba: LbaLe::from_u64(STATE_END_LBA),
            name: "STATE".parse().unwrap(),
            ..Default::default()
        };

        let new_state_range = shrink_partition_by(mock_stateful_info, NEW_PART_SIZE_BLOCKS);
        assert!(new_state_range.is_ok());
        let new_state_range = new_state_range.unwrap();
        assert_eq!(new_state_range.start().to_u64(), STATE_START_LBA);
        assert_eq!(
            new_state_range.end().to_u64(),
            STATE_END_LBA - NEW_PART_SIZE_BLOCKS
        );

        let new_part_range = add_partition_after(new_state_range, NEW_PART_SIZE_BLOCKS);
        assert!(new_part_range.is_ok());
        let new_part_range = new_part_range.unwrap();
        assert_eq!(
            new_part_range.start().to_u64(),
            STATE_END_LBA - NEW_PART_SIZE_BLOCKS + 1
        );
        assert_eq!(new_part_range.end().to_u64(), STATE_END_LBA);
    }

    #[test]
    fn test_insert_partition_fails() {
        const STATE_START: u64 = 0;
        // Assuming a block size of 512.
        const STATE_END: u64 = 10_000_000_000 / 512;
        const NEW_PART_SIZE: u64 = 11_000_000_000 / 512;

        let mock_stateful_info = GptPartitionEntry {
            starting_lba: LbaLe::from_u64(STATE_START),
            ending_lba: LbaLe::from_u64(STATE_END),
            name: "STATE".parse().unwrap(),
            ..Default::default()
        };

        let new_state_range = shrink_partition_by(mock_stateful_info, NEW_PART_SIZE);
        assert!(new_state_range.is_err());
    }

    #[test]
    fn test_merge_partition() {
        const PART_ONE_START: u64 = 0;
        const PART_ONE_END: u64 = 1;

        const PART_TWO_START: u64 = 2;
        const PART_TWO_END: u64 = 3;

        let part_one_range =
            LbaRangeInclusive::new(Lba(PART_ONE_START), Lba(PART_ONE_END)).unwrap();
        let part_two_range =
            LbaRangeInclusive::new(Lba(PART_TWO_START), Lba(PART_TWO_END)).unwrap();

        let result = merge_partition_ranges(part_one_range, part_two_range);
        assert!(result.is_ok());

        let result = result.unwrap();
        assert_eq!(result.start().to_u64(), PART_ONE_START);
        assert_eq!(result.end().to_u64(), PART_TWO_END);
    }

    #[test]
    fn test_merge_partition_fails() {
        const PART_ONE_START: u64 = 0;
        const PART_ONE_END: u64 = 1;

        const PART_TWO_START: u64 = 1;
        const PART_TWO_END: u64 = 2;

        let part_one_range =
            LbaRangeInclusive::new(Lba(PART_ONE_START), Lba(PART_ONE_END)).unwrap();
        let part_two_range =
            LbaRangeInclusive::new(Lba(PART_TWO_START), Lba(PART_TWO_END)).unwrap();

        let result = merge_partition_ranges(part_one_range, part_two_range);
        assert!(result.is_err());
    }
}
