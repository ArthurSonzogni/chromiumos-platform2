// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{path::Path, process::ExitCode};

use anyhow::{bail, Context, Result};
use gpt_disk_types::{guid, Guid};
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

const FLEX_DEPLOY_PART_NUM_BLOCKS: u64 = 8_000_000_000 / 512;
const FLEX_DEPLOY_PART_LABEL: &str = "FLEX_DEPLOY";
const FLEX_DEPLOY_PART_NUM: u32 = 13;

const STATEFUL_PARTITION_LABEL: &str = "STATE";
const STATEFUL_PARTITION_NUM: u32 = 1;

const DATA_PART_TYPE: Guid = guid!("e160967d-9493-4ba8-8153-f0dc8ac4f7b7");

/// Copies the ChromeOS Flex image to rootfs (residing in RAM). This is done
/// since we are about to repartition the disk and can't loose the image. Since
/// the image size is about 2.5GB, we assume that much free space in RAM.
fn copy_image_to_rootfs(disk_path: &Path) -> Result<()> {
    // We expect our data on a partition with [`DATA_PART_GUID`], with a vFAT filesystem.
    let data_partition_path =
        disk::get_data_partition(disk_path).context("Unable to find correct partition path")?;
    let mount = mount::Mount::mount_by_path(&data_partition_path, mount::FsType::Vfat)
        .context("Unable to mount data partition")?;

    // Copy the image to rootfs.
    std::fs::copy(
        mount.mount_path().join(FLEX_IMAGE_FILENAME),
        Path::new("/root").join(FLEX_IMAGE_FILENAME),
    )
    .context("Unable to copy image to rootfs")?;

    Ok(())
}

/// Setup the disk for a ChromeOS Flex installation performing the following
/// two steps:
/// 1. Put the ChromeOS partition layout and write stateful partition.
/// 2. Insert a thirteenth partition on disk for our own data.
fn setup_disk(disk_path: &Path) -> Result<()> {
    // Install the layout and stateful partition.
    chromeos_install::write_partition_table_and_stateful(disk_path)
        .context("Unable to put initial partition table")?;
    // Insert a thirtheenth partition.
    disk::insert_thirteenth_partition(disk_path)
        .context("Unable to insert thirtheenth partition")?;
    // Reread the partition table.
    disk::reload_partitions(disk_path).context("Unable to reload partition table")
}

/// Sets up the thirteenth partition on disk and then proceeds to install the
/// provided image on the device.
fn setup_flex_deploy_partition_and_install(disk_path: &Path) -> Result<()> {
    // Create an ext4 filesystem on the disk.
    let new_partition_path =
        libchromeos::disk::get_partition_device(disk_path, FLEX_DEPLOY_PART_NUM)
            .context("Unable to find correct partition path")?;
    disk::mkfs_ext4(new_partition_path.as_path())
        .context("Unable to write ext4 to the flex deployment partition")?;

    let new_part_mount =
        mount::Mount::mount_by_path(new_partition_path.as_path(), mount::FsType::EXT4)
            .context("Unable to mount flex deployment partition")?;

    // Then uncompress the image on disk.
    let entries = util::uncompress_tar_xz(
        &Path::new("/root").join(FLEX_IMAGE_FILENAME),
        new_part_mount.mount_path(),
    )
    .context("Unable to uncompress the image")?;
    // A compressed ChromeOS image only contains the image path.
    let image_path = entries
        .get(0)
        .context("Got malformed ChromeOS Flex image")?;

    // Finally install the image on disk.
    chromeos_install::install_image_to_disk(
        disk_path,
        new_part_mount.mount_path().join(image_path).as_path(),
    )
    .context("Unable to install the image to disk")
}

/// Performs the actual installation of ChromeOS.
fn perform_installation(disk_path: &Path) -> Result<()> {
    info!("Setting up the disk");
    setup_disk(disk_path)?;

    info!("Setting up the new partition and installing ChromeOS Flex");
    setup_flex_deploy_partition_and_install(disk_path)?;

    info!("Trying to remove the flex deployment partition");
    disk::try_remove_thirteenth_partition(disk_path)
}

/// Installs ChromeOS Flex and retries the actual installation steps at most three times.
fn run(disk_path: &Path) -> Result<()> {
    info!("Start Flex-ing");
    copy_image_to_rootfs(disk_path)?;

    // Try installing on the device three times at most.
    for _ in 0..3 {
        match perform_installation(disk_path) {
            Ok(_) => {
                // On success we reboot and end execution.
                info!("Rebooting into ChromeOS Flex, keep fingers crossed");
                reboot(nix::sys::reboot::RebootMode::RB_AUTOBOOT)
                    .context("Unable to reboot after successful installation")?;
                return Ok(());
            }
            Err(err) => {
                error!("Flexor couldn't complete due to error: {err}");
            }
        }
    }

    Ok(())
}

/// Tries to save logs to the disk depending on what state the installation fails in.
/// We basically have two option:
/// 1. Either we are in the state before the disk was reformatted, in that case we write
///    the logs back to the partition that also has the installation payload.
/// 2. Otherwise we hope to already have the Flex layout including the FLEX_DEPLOY partition
///    in that case we write the logs to that partition (may need to create a filesystem on that
///    partition though).
fn try_safe_logs(disk_path: &Path) -> Result<()> {
    // Case 1: The data partition still exists, so we write the logs to it.
    if let Ok(data_partition_path) = disk::get_data_partition(disk_path) {
        if matches!(data_partition_path.try_exists(), Ok(true)) {
            let data_mount =
                mount::Mount::mount_by_path(&data_partition_path, mount::FsType::Vfat)?;
            std::fs::copy(FLEXOR_LOG_FILE, data_mount.mount_path())
                .context("Unable to copy the logfile to the data partition")?;
            return Ok(());
        }
    }

    let flex_depl_partition_path =
        libchromeos::disk::get_partition_device(disk_path, FLEX_DEPLOY_PART_NUM)
            .context("Error finding a place to write logs to")?;

    // Case 2: We already have the Flex layout and can try to write to the FLEX_DEPLOY partition.
    if let Ok(true) = flex_depl_partition_path.try_exists() {
        match mount::Mount::mount_by_path(&flex_depl_partition_path, mount::FsType::EXT4) {
            Ok(flex_depl_mount) => {
                std::fs::copy(FLEXOR_LOG_FILE, flex_depl_mount.mount_path())
                    .context("Unable to copy the logfile to the flex deployment partition")?;
            }
            Err(_) => {
                // The partition seems to exist, but we can't mount it as ext4,
                // so we try to create a file system and retry.
                disk::mkfs_ext4(&flex_depl_partition_path)?;
                let flex_depl_mount =
                    mount::Mount::mount_by_path(&flex_depl_partition_path, mount::FsType::EXT4)?;
                std::fs::copy(FLEXOR_LOG_FILE, flex_depl_mount.mount_path()).context(
                    "Unable to copy the logfile to the formatted flex deployment partition",
                )?;
            }
        }
    } else {
        bail!(
            "Unable to write logs since neither the data partition
             nor the flex deployment partition exist"
        );
    }

    Ok(())
}

fn main() -> ExitCode {
    // Setup the panic handler and logging.
    panic_handler::install_memfd_handler();
    if let Err(err) = syslog::init(FLEXOR_TAG.to_owned(), /*log_to_stderr=*/ true) {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Hello from Flexor!");
    let disk_path = match disk::get_target_device().context("Error getting the target disk") {
        Ok(path) => path,
        Err(err) => {
            error!("Error selecting the target disk: {err}");
            return ExitCode::FAILURE;
        }
    };

    if let Err(err) = run(&disk_path) {
        error!("Unable to perform installation due to error: {err}");

        // If we weren't successful, try to save the logs.
        if let Err(err) = try_safe_logs(&disk_path) {
            error!("Unable to save logs due to: {err}")
        }
        // TODO(b/314965086): Add an error screen displaying the log.
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}
