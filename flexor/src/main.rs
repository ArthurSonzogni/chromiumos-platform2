// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    os::unix::fs::PermissionsExt,
    path::{Path, PathBuf},
    process::ExitCode,
};

use anyhow::{anyhow, bail, Context, Result};
use libchromeos::mount::{self, FsType, Mount};
use libchromeos::panic_handler;
use log::{error, info};
use nix::sys::{reboot::reboot, stat::Mode};

mod cgpt;
mod chromeos_install;
mod disk;
mod gpt;
mod logger;
mod lsblk;
mod metrics;
mod util;

const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEX_CONFIG_SRC_FILENAME: &str = "flex_config.json";
const FLEX_CONFIG_TARGET_FILEPATH: &str = "unencrypted/flex_config/config.json";
const FLEXOR_LOG_FILE: &str = "/var/log/messages";
const ROOTFS_DIR: &str = "/root";

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
/// disk and can't lose the image. Since the image size is about 2.5GB, we
/// assume that much free space in RAM.
fn copy_installation_files_to_rootfs(config: &InstallConfig) -> Result<()> {
    let mount = mount::Builder::new(&config.install_partition)
        .fs_type(FsType::Vfat)
        .temp_backed_mount()
        .context("Unable to mount the install partition")?;
    let root = Path::new(ROOTFS_DIR);

    // Copy the image to rootfs.
    std::fs::copy(
        mount.mount_path().join(FLEX_IMAGE_FILENAME),
        root.join(FLEX_IMAGE_FILENAME),
    )
    .context("Unable to copy image to rootfs")?;

    // Copy optional files to the rootfs.
    for file in [FLEX_CONFIG_SRC_FILENAME, metrics::FLEXOR_INSTALL_TYPE_FILE] {
        if std::fs::copy(mount.mount_path().join(file), root.join(file)).is_err() {
            info!("Unable to copy {file} to rootfs, proceeding since it is optional");
        }
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

    let new_part_mount = mount::Builder::new(new_partition_path.as_path())
        .fs_type(FsType::Ext4)
        .temp_backed_mount()
        .context("Unable to mount flex deployment partition")?;

    // Then uncompress the image on disk.
    let entries = util::uncompress_tar_xz(
        &Path::new(ROOTFS_DIR).join(FLEX_IMAGE_FILENAME),
        new_part_mount.mount_path(),
    )
    .context("Unable to uncompress the image")?;
    // A compressed ChromeOS image only contains the image path.
    let image_path = entries
        .first()
        .context("Got malformed ChromeOS Flex image")?;

    // Needed for managing boot entries in postinst.
    let _efivarfs = chromeos_install::mount_efivarfs().context("Unable to mount efivarfs")?;
    if chromeos_install::maybe_set_uefi_entry_name().is_err() {
        info!("Couldn't set UEFI boot entry name, using default.");
    }

    // Finally install the image on disk.
    chromeos_install::install_image_to_disk(
        &config.target_device,
        new_part_mount.mount_path().join(image_path).as_path(),
    )
    .context("Unable to install the image to disk")
}

/// Finds and mounts the stateful partition.
///
/// Returns the mount object holding stateful open.
fn mount_stateful(device: &Path) -> Result<Mount> {
    let stateful_partition =
        &libchromeos::disk::get_partition_device(device, disk::STATEFUL_PARTITION_NUM)
            .context("Unable to find stateful partition")?;

    Ok(mount::Builder::new(stateful_partition)
        .fs_type(FsType::Ext4)
        .temp_backed_mount()?)
}

/// Copies the flex config to stateful partition.
fn copy_flex_config_to_stateful(stateful: &Path) -> Result<()> {
    let config_path = stateful.join(FLEX_CONFIG_TARGET_FILEPATH);

    // Create the new `flex_config` folder and copy the file.
    nix::unistd::mkdir(
        config_path.parent().unwrap(),
        Mode::from_bits(FLEX_CONFIG_DIR_PERM).unwrap(),
    )?;
    std::fs::copy(
        Path::new(ROOTFS_DIR).join(FLEX_CONFIG_SRC_FILENAME),
        &config_path,
    )
    .context("Unable to copy config")?;

    // Set correct owner for both the new `flex_config` folder and `config.json` file.
    util::set_owner(
        config_path.parent().unwrap(),
        OOBE_CFG_RESTORE_UID,
        OOBE_CFG_RESTORE_GID,
    )?;
    util::set_owner(&config_path, OOBE_CFG_RESTORE_UID, OOBE_CFG_RESTORE_GID)?;

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

    let stateful = mount_stateful(&config.target_device)?;
    let root_dir = Path::new(ROOTFS_DIR);

    if metrics::write_install_method_to_stateful(root_dir, stateful.mount_path()).is_err() {
        info!("Couldn't indicate install method. Install metric won't be sent from Flex.")
    }

    if root_dir.join(FLEX_CONFIG_SRC_FILENAME).exists() {
        if let Err(err) = copy_flex_config_to_stateful(stateful.mount_path()) {
            error!("Unable to copy Flex config due to: {err}");
            // If this fails, we can't do anything about it, so just log the error.
            if let Err(err) = try_save_logs(config) {
                error!("Unable to save logs due to: {err:#}");
            }
        } else {
            info!("Successfully copied a flex config to stateful partition.");
        }
        return Ok(());
    }

    info!("Didn't find a flex config to copy to stateful partition, still proceeding");
    Ok(())
}

/// Installs ChromeOS Flex and retries the actual installation steps at most three times.
fn run(config: &InstallConfig) -> Result<()> {
    copy_installation_files_to_rootfs(config)?;

    // Try installing on the device three times at most.
    let mut return_err = anyhow!("");
    for attempt in 1..=3 {
        info!("Starting attempt number {attempt} to install ChromeOS Flex.");
        match perform_installation(config) {
            Ok(_) => {
                // On success we reboot and end execution.
                info!("Install successful, rebooting into ChromeOS Flex");
                reboot(nix::sys::reboot::RebootMode::RB_AUTOBOOT)
                    .context("Unable to reboot after successful installation")?;
                return Ok(());
            }
            Err(err) => {
                error!("Flexor couldn't complete due to error: {err}");
                return_err = err;
            }
        }
    }

    Err(return_err)
}

/// Tries to save logs to the disk depending on what state the installation fails in.
/// We basically have two option:
/// 1. Either we are in the state before the disk was reformatted, in that case we write
///    the logs back to the partition that also has the installation payload.
/// 2. Otherwise we hope to already have the Flex layout including the FLEX_DEPLOY partition
///    in that case we write the logs to that partition (may need to create a filesystem on that
///    partition though).
fn try_save_logs(config: &InstallConfig) -> Result<()> {
    info!("Attempting to save logs to disk.");

    fn copy_log(out_dir: &Path, decription: &str) -> Result<()> {
        let logfile = out_dir.join("install_log");
        std::fs::copy(FLEXOR_LOG_FILE, logfile)
            .with_context(|| format!("Unable to copy the logfile to the {decription}"))?;
        Ok(())
    }

    // Case 1: The install partition still exists, so we write the logs to it.
    // There should be a partition at this path in either case, so to confirm that it's the install
    // partition we try to mount it as VFAT: in Case 2 it probably won't be.
    if config.install_partition.exists() {
        let install_mount = mount::Builder::new(&config.install_partition)
            .fs_type(FsType::Vfat)
            .temp_backed_mount();
        if let Ok(install_mount) = install_mount {
            copy_log(install_mount.mount_path(), "install partition")?;
            return Ok(());
        }
        // If it exists, but we couldn't mount it it's probably a new partition and we're in Case 2.
    }

    let flex_depl_partition_path =
        libchromeos::disk::get_partition_device(&config.target_device, disk::FLEX_DEPLOY_PART_NUM)
            .context("Error finding a place to write logs to")?;

    // Case 2: We already have the Flex layout and can try to write to the FLEX_DEPLOY partition.
    if flex_depl_partition_path.exists() {
        match mount::Builder::new(&flex_depl_partition_path)
            .fs_type(FsType::Ext4)
            .temp_backed_mount()
        {
            Ok(flex_depl_mount) => {
                copy_log(flex_depl_mount.mount_path(), "flex deployment partition")?;
            }
            Err(_) => {
                // The partition seems to exist, but we can't mount it as ext4,
                // so we try to create a file system and retry.
                disk::mkfs_ext4(&flex_depl_partition_path)?;
                let flex_depl_mount = mount::Builder::new(&flex_depl_partition_path)
                    .fs_type(FsType::Ext4)
                    .temp_backed_mount()?;
                copy_log(
                    flex_depl_mount.mount_path(),
                    "formatted flex deployment partition",
                )?;
            }
        }
        return Ok(());
    }

    bail!(
        "Unable to write logs since neither the data partition
         nor the flex deployment partition exist"
    );
}

fn main() -> ExitCode {
    // Setup the panic handler and logging.
    panic_handler::install_memfd_handler();
    if let Err(err) = logger::init() {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Installing ChromeOS Flex, please wait and don't turn off the device");

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
        if let Err(err) = try_save_logs(&config) {
            error!("Unable to save logs due to: {err:#}")
        }
        // TODO(b/314965086): Add an error screen displaying the log.
        ExitCode::FAILURE
    } else {
        info!("Successfully installed, rebooting");
        ExitCode::SUCCESS
    }
}
