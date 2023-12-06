// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{Context, Result};
use gpt_disk_types::BlockSize;
use libchromeos::{panic_handler, syslog};
use log::{error, info};
use nix::sys::reboot::reboot;

mod cgpt;
mod chromeos_install;
mod disk;
mod gpt;
mod lsblk;
mod mount;
mod util;

const FLEXOR_TAG: &str = "flexor";
const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEXOR_LOG_FILE: &str = "/var/log/messages";
const DATA_PART_NUM: u32 = 4;
const FLEX_DEPLOY_PART_NUM: u32 = 13;

/// Copies the ChromeOS Flex image to rootfs (residing in RAM). This is done
/// since we are about to repartition the disk and can't loose the image. Since
/// the image size is about 2.5GB, we assume that much free space in RAM.
fn copy_image_to_rootfs(device_path: &Path) -> Result<()> {
    // We expect our data in partition 4, with a vFAT filesystem.
    let data_partition_path = libchromeos::disk::get_partition_device(device_path, DATA_PART_NUM)
        .context("Unable to find correct partition path")?;
    let mount = mount::Mount::mount_by_path(data_partition_path, mount::FsType::Vfat)?;

    // Copy the image to rootfs.
    std::fs::copy(
        mount.mount_path().join(FLEX_IMAGE_FILENAME),
        Path::new("/root").join(FLEX_IMAGE_FILENAME),
    )?;

    Ok(())
}

/// Setup the disk for a ChromeOS Flex installation performing the following
/// two steps:
/// 1. Put the ChromeOS partition layout and write stateful partition.
/// 2. Insert a thirteenth partition on disk for our own data.
fn setup_disk(device_path: &Path, block_size: BlockSize) -> Result<()> {
    // Install the layout and stateful partition.
    chromeos_install::write_partition_table_and_stateful(device_path)?;
    // Insert a thirtheenth partition.
    util::insert_thirteenth_partition(device_path, block_size)?;
    // Reread the partition table.
    util::reload_partitions(device_path)
}

/// Sets up the thirteenth partition on disk and then proceeds to install the
/// provided image on the device.
fn setup_flex_deploy_partition_and_install(device_path: &Path) -> Result<()> {
    // Create an ext4 filesystem on the disk.
    let new_partition_path =
        libchromeos::disk::get_partition_device(device_path, FLEX_DEPLOY_PART_NUM)
            .context("Unable to find correct partition path")?;
    util::mkfs_ext4(new_partition_path.as_path())?;
    let new_part_mount =
        mount::Mount::mount_by_path(new_partition_path.as_path(), mount::FsType::EXT4)?;

    // Then uncompress the image on disk.
    let entries = util::uncompress_tar_xz(
        Path::new("/root").join(FLEX_IMAGE_FILENAME),
        new_part_mount.mount_path(),
    )?;
    // A compressed ChromeOS image only contains the image path.
    let image_path = entries
        .get(0)
        .context("Got malformed ChromeOS Flex image")?;

    // Finally install the image on disk.
    chromeos_install::install_image_to_disk(
        device_path,
        new_part_mount.mount_path().join(image_path).as_path(),
    )
}

/// Performs the actual installation of ChromeOS.
fn perform_installation(device_path: &Path) -> Result<()> {
    info!("Start Flex-ing");
    copy_image_to_rootfs(device_path)?;

    info!("Setting up the disk");
    // Assuming a block size of 512, like all of ChromeOS.
    setup_disk(device_path, BlockSize::BS_512)?;

    info!("Setting up the new partition and installing ChromeOS Flex");
    setup_flex_deploy_partition_and_install(device_path)
}

/// Tries to save logs to the disk depending on what state the installation fails in.
/// We basically have two option:
/// 1. Either we are in the state before the disk was reformatted, in that case we write
///    the logs back to the partition that also has the installation payload.
/// 2. Otherwise we hope to already have the Flex layout including the FLEX_DEPLOY partition
///    in that case we write the logs to that partition (may need to create a filesystem on that
///    partition though).
fn try_safe_logs(device_path: &Path) -> Result<()> {
    // Case 1: The data partition still exists, so we write the logs to it.
    if let Some(data_partition_path) =
        libchromeos::disk::get_partition_device(device_path, DATA_PART_NUM)
    {
        if matches!(data_partition_path.try_exists(), Ok(true)) {
            let data_mount = mount::Mount::mount_by_path(data_partition_path, mount::FsType::Vfat)?;
            std::fs::copy(FLEXOR_LOG_FILE, data_mount.mount_path())?;
            return Ok(());
        }
    }

    let flex_depl_partition_path =
        libchromeos::disk::get_partition_device(device_path, FLEX_DEPLOY_PART_NUM)
            .context("Error finding a place to write logs to")?;

    // Case 2: We already have the Flex layout and can try to write to the FLEX_DEPLOY partition.
    if let Ok(true) = flex_depl_partition_path.try_exists() {
        match mount::Mount::mount_by_path(&flex_depl_partition_path, mount::FsType::EXT4) {
            Ok(flex_depl_mount) => {
                std::fs::copy(FLEXOR_LOG_FILE, flex_depl_mount.mount_path())?;
            }
            Err(_) => {
                // The partition seems to exist, but we can't mount it as ext4,
                // so we try to create a file system and retry.
                util::mkfs_ext4(&flex_depl_partition_path)?;
                let flex_depl_mount =
                    mount::Mount::mount_by_path(&flex_depl_partition_path, mount::FsType::EXT4)?;
                std::fs::copy(FLEXOR_LOG_FILE, flex_depl_mount.mount_path())?;
            }
        }
    }

    Ok(())
}

fn main() -> Result<()> {
    // Setup the panic handler and logging.
    panic_handler::install_memfd_handler();
    if let Err(err) = syslog::init(FLEXOR_TAG.to_owned(), /*log_to_stderr=*/ true) {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Hello from Flexor!");
    let device_path = disk::get_target_device().context("Error getting the target disk")?;

    match perform_installation(&device_path) {
        Ok(_) => {
            info!("Rebooting into ChromeOS Flex, keep fingers crossed");
            reboot(nix::sys::reboot::RebootMode::RB_AUTOBOOT)
                .context("Unable to reboot after successful installation")?;
            Ok(())
        }
        Err(err) => {
            error!("Flexor couldn't complete due to error: {err}");
            // Try to save logs if possible, otherwise just be stuck.
            let _ = try_safe_logs(&device_path);
            // TODO(b/314965086): Add an error screen displaying the log.
            Ok(())
        }
    }
}
