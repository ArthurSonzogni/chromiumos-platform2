// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    path::{Path, PathBuf},
    process::ExitCode,
};

use anyhow::{bail, Context, Result};
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

// This struct contains all of the information commonly passed around
// during an installation.
struct InstallConfig {
    target_device: PathBuf,
    install_partition: PathBuf,
}

impl InstallConfig {
    pub fn new() -> Result<Self> {
        let disk_info = disk::disk_info()?;

        Ok(InstallConfig {
            target_device: disk_info.target_device,
            install_partition: disk_info.install_partition,
        })
    }
}

/// Copies the ChromeOS Flex image to rootfs (residing in RAM). This is done
/// since we are about to repartition the disk and can't loose the image. Since
/// the image size is about 2.5GB, we assume that much free space in RAM.
fn copy_image_to_rootfs(config: &InstallConfig) -> Result<()> {
    // We expect our data on a partition with a vFAT filesystem.
    let mount = mount::Mount::mount_by_path(&config.target_device, mount::FsType::Vfat)
        .context("Unable to mount the install partition")?;

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
fn setup_disk(config: &InstallConfig) -> Result<()> {
    // Install the layout and stateful partition.
    chromeos_install::write_partition_table_and_stateful(&config.target_device)
        .context("Unable to put initial partition table")?;
    // Insert a thirtheenth partition.
    disk::insert_thirteenth_partition(&config.target_device)
        .context("Unable to insert thirtheenth partition")?;
    // Reread the partition table.
    disk::reload_partitions(&config.target_device).context("Unable to reload partition table")
}

/// Sets up the thirteenth partition on disk and then proceeds to install the
/// provided image on the device.
fn setup_flex_deploy_partition_and_install(config: &InstallConfig) -> Result<()> {
    // Create an ext4 filesystem on the disk.
    let new_partition_path =
        libchromeos::disk::get_partition_device(&config.target_device, disk::FLEX_DEPLOY_PART_NUM)
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
        &config.target_device,
        new_part_mount.mount_path().join(image_path).as_path(),
    )
    .context("Unable to install the image to disk")
}

/// Performs the actual installation of ChromeOS.
fn perform_installation(config: &InstallConfig) -> Result<()> {
    info!("Setting up the disk");
    setup_disk(config)?;

    info!("Setting up the new partition and installing ChromeOS Flex");
    setup_flex_deploy_partition_and_install(config)?;

    info!("Trying to remove the flex deployment partition");
    disk::try_remove_thirteenth_partition(&config.target_device)
}

/// Installs ChromeOS Flex and retries the actual installation steps at most three times.
fn run(config: &InstallConfig) -> Result<()> {
    info!("Start Flex-ing");
    copy_image_to_rootfs(config)?;

    // Try installing on the device three times at most.
    for _ in 0..3 {
        match perform_installation(config) {
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
fn try_safe_logs(config: &InstallConfig) -> Result<()> {
    // Case 1: The install partition still exists, so we write the logs to it.
    if matches!(config.install_partition.try_exists(), Ok(true)) {
        let install_mount =
            mount::Mount::mount_by_path(&config.install_partition, mount::FsType::Vfat)?;
        std::fs::copy(FLEXOR_LOG_FILE, install_mount.mount_path())
            .context("Unable to copy the logfile to the install partition")?;
        return Ok(());
    }

    let flex_depl_partition_path =
        libchromeos::disk::get_partition_device(&config.target_device, disk::FLEX_DEPLOY_PART_NUM)
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
    let config = match InstallConfig::new() {
        Ok(config) => config,
        Err(err) => {
            error!("Error getting information for the install: {err}");
            return ExitCode::FAILURE;
        }
    };

    if let Err(err) = run(&config) {
        error!("Unable to perform installation due to error: {err}");

        // If we weren't successful, try to save the logs.
        if let Err(err) = try_safe_logs(&config) {
            error!("Unable to save logs due to: {err}")
        }
        // TODO(b/314965086): Add an error screen displaying the log.
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}
