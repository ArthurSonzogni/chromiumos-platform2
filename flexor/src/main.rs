// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fs,
    io::Write,
    os::unix::fs::{chown, PermissionsExt},
    path::{Path, PathBuf},
    process::ExitCode,
};

use anyhow::{bail, Context, Result};
use libchromeos::{panic_handler, syslog};
use log::{error, info};
use nix::sys::{reboot::reboot, stat::Mode};

mod cgpt;
mod chromeos_install;
mod disk;
mod gpt;
mod lsblk;
mod mount;
mod util;

const FLEXOR_TAG: &str = "flexor";
const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEX_CONFIG_SRC_FILENAME: &str = "flex_config.json";
const FLEX_CONFIG_TARGET_FILEPATH: &str = "unencrypted/flex_config/config.json";
const FLEXOR_LOG_FILE: &str = "/var/log/messages";

// User ID and Group ID for the oobe_config_restore user in ChromeOS.
const OOBE_CFG_RESTORE_UID: u32 = 20121;
const OOBE_CFG_RESTORE_GID: u32 = 20121;

// File and folder permissions for the Flex config.
const FLEX_CONFIG_DIR_PERM: u32 = 0o740;
const FLEX_CONFIG_FILE_PERM: u32 = 0o640;

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

/// Copies the ChromeOS Flex image and other install files (e.g. config) to
/// rootfs (residing in RAM). This is done since we are about to repartition the
/// disk and can't loose the image. Since the image size is about 2.5GB, we
/// assume that much free space in RAM.
fn copy_installation_files_to_rootfs(config: &InstallConfig) -> Result<()> {
    let mount = mount::Mount::mount_by_path(&config.install_partition, mount::FsType::Vfat)
        .context("Unable to mount the install partition")?;

    // Copy the image to rootfs.
    std::fs::copy(
        mount.mount_path().join(FLEX_IMAGE_FILENAME),
        Path::new("/root").join(FLEX_IMAGE_FILENAME),
    )
    .context("Unable to copy image to rootfs")?;

    // Copy the config to rootfs.
    if std::fs::copy(
        mount.mount_path().join(FLEX_CONFIG_SRC_FILENAME),
        Path::new("root").join(FLEX_CONFIG_SRC_FILENAME),
    )
    .is_err()
    {
        info!("Unable to copy flex config to rootfs, proceeding since it is optional");
    }

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
        .first()
        .context("Got malformed ChromeOS Flex image")?;

    // Finally install the image on disk.
    chromeos_install::install_image_to_disk(
        &config.target_device,
        new_part_mount.mount_path().join(image_path).as_path(),
    )
    .context("Unable to install the image to disk")
}

/// Copies the flex config to stateful partition.
fn copy_flex_config_to_stateful(config: &InstallConfig) -> Result<()> {
    let stateful_mount = mount::Mount::mount_by_path(
        &libchromeos::disk::get_partition_device(
            &config.target_device,
            disk::STATEFUL_PARTITION_NUM,
        )
        .context("Unable to find stateful partition")?,
        mount::FsType::EXT4,
    )?;

    let config_path = stateful_mount
        .mount_path()
        .join(FLEX_CONFIG_TARGET_FILEPATH);

    // Create the new `flex_config` folder and copy the file.
    nix::unistd::mkdir(
        config_path.parent().unwrap(),
        Mode::from_bits(FLEX_CONFIG_DIR_PERM).unwrap(),
    )?;
    std::fs::copy(
        Path::new("root").join(FLEX_CONFIG_SRC_FILENAME),
        &config_path,
    )
    .context("Unable to copy config")?;

    // Set correct owner for both the new `flex_config` folder and
    // `config.json` file.
    fn set_oobe_cfg_restore_owner(path: &Path) -> Result<()> {
        chown(path, Some(OOBE_CFG_RESTORE_UID), Some(OOBE_CFG_RESTORE_GID)).context(format!(
            "Unable to set correct owner for {}",
            path.display()
        ))
    }
    set_oobe_cfg_restore_owner(config_path.parent().unwrap())?;
    set_oobe_cfg_restore_owner(&config_path)?;

    // Set the correct file permissions for the config.
    let mut perm = std::fs::metadata(&config_path)?.permissions();
    perm.set_mode(FLEX_CONFIG_FILE_PERM);
    std::fs::set_permissions(&config_path, perm)?;

    Ok(())
}

/// Performs the actual installation of ChromeOS.
fn perform_installation(config: &InstallConfig) -> Result<()> {
    info!("Setting up the disk");
    setup_disk(config)?;

    info!("Setting up the new partition and installing ChromeOS Flex");
    setup_flex_deploy_partition_and_install(config)?;

    info!("Trying to remove the flex deployment partition");
    disk::try_remove_thirteenth_partition(&config.target_device)?;

    if matches!(
        Path::new("root")
            .join(FLEX_CONFIG_SRC_FILENAME)
            .try_exists(),
        Ok(true)
    ) {
        if let Err(err) = copy_flex_config_to_stateful(config) {
            error!("Unable to copy Flex config due to: {err}");
            // If this fails, we can't do anything about it, so ignore the error.
            let _ = try_safe_logs(config);
        } else {
            info!("Successfully copied a flex config.");
        }
        return Ok(());
    }

    info!("Didn't find a flex config to copy, still proceeding");
    Ok(())
}

/// Installs ChromeOS Flex and retries the actual installation steps at most three times.
fn run(config: &InstallConfig) -> Result<()> {
    info!("Start Flex-ing");
    copy_installation_files_to_rootfs(config)?;

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

// Tries to log to the Kernel messages with the highest priority log level (0).
fn try_log_to_kmsg(msg: &str) {
    let kmsg = format!("<0>{msg}");
    let Ok(mut kmsg_fd) = fs::OpenOptions::new().append(true).open("/dev/kmsg") else {
        return;
    };
    let _ = kmsg_fd.write_all(kmsg.as_bytes());
}

fn main() -> ExitCode {
    // Setup the panic handler and logging.
    panic_handler::install_memfd_handler();
    if let Err(err) = syslog::init(FLEXOR_TAG.to_owned(), /*log_to_stderr=*/ true) {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Hello from Flexor!");
    try_log_to_kmsg("Installing ChromeOS Flex, please wait and don't turn off the device");

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
        try_log_to_kmsg("Successfully installed, rebooting");
        ExitCode::SUCCESS
    }
}
