// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use gpt_disk_types::{BlockSize, GptPartitionEntry, Lba, LbaRangeInclusive};
use log::info;
use std::fs::File;
use std::process::Command;

use crate::cgpt;
use crate::gpt;
use crate::mount;

use crate::util::execute_command;

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

const NEW_PARTITION_SIZE_BYTES: u64 = 8_000_000_000;
const NEW_PARTITION_NAME: &str = "FLEX_DEPLOY";

/// Inserts a thirtheenth partition after the stateful partition (shrinks
/// stateful partition). This can only be called with a disk that already
/// has a ChromeOS partition layout. Since this method is just changing
/// the partition layout but not the filesystem, it assumes the filesystem
/// on stateful partition will be re-created later.
pub fn insert_thirteenth_partition(disk_path: &Path) -> Result<()> {
    let file = File::open(disk_path)?;
    let mut gpt = gpt::Gpt::from_file(file, BlockSize::BS_512)?;

    let new_part_size_lba = NEW_PARTITION_SIZE_BYTES / BlockSize::BS_512.to_u64();
    let stateful_name = "STATE";

    let current_stateful = gpt
        .get_entry_for_partition_with_label(stateful_name.parse().unwrap())?
        .context("Unable to locate stateful partition on disk")?;

    let new_stateful_range = shrink_partition_by(current_stateful, new_part_size_lba)
        .context("Unable to shrink stateful partiton")?;
    cgpt::resize_cgpt_partition(1, disk_path, stateful_name, new_stateful_range)?;

    let new_range = add_partition_after(new_stateful_range, new_part_size_lba)
        .context("Unable to calculate new partition range")?;
    cgpt::add_cgpt_partition(13, disk_path, NEW_PARTITION_NAME, new_range)
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

fn add_partition_after(range: LbaRangeInclusive, size_in_lba: u64) -> Result<LbaRangeInclusive> {
    let new_part_range = LbaRangeInclusive::new(
        Lba(range.end().to_u64() + 1),
        Lba(range.end().to_u64() + size_in_lba),
    )
    .context("Error calculating partition range")?;

    Ok(new_part_range)
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
    let Some(flex_depl_part) = libchromeos::disk::get_partition_device(path, crate::DATA_PART_NUM)
    else {
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

#[cfg(test)]
mod tests {
    use super::add_partition_after;
    use super::shrink_partition_by;
    use gpt_disk_types::{GptPartitionEntry, LbaLe};

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
}
