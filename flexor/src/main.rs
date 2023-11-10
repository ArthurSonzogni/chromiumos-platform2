// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{bail, Context, Result};
use clap::Parser;
use gpt_disk_types::BlockSize;
use libchromeos::panic_handler;
use log::info;
use nix::sys::reboot::reboot;

mod cgpt;
mod chromeos_install;
mod gpt;
mod mount;
mod util;

const FLEXOR_TAG: &str = "flexor";
const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const DATA_PART_NUM: u32 = 4;
const FLEX_DEPLOY_PART_NUM: u32 = 13;

/// Runs the Flexor: A ChromeOS Flex installer.
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Path of the disk we want to install ChromeOS Flex on.
    #[arg(short, long)]
    device: String,
}

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

fn main() -> Result<()> {
    // Setup the panic handler.
    panic_handler::install_memfd_handler();
    // For now log everything.
    log::set_max_level(log::LevelFilter::Info);
    if let Err(err) = stderrlog::new()
        .module(FLEXOR_TAG)
        .show_module_names(true)
        .verbosity(log::max_level())
        .init()
    {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Hello from Flexor!");
    let args = Args::parse();

    let device_path = Path::new(&args.device);
    if !matches!(device_path.try_exists(), Ok(true)) {
        bail!("Unable to locate device {}", device_path.display());
    }

    // Assuming a block size of 512, like all of ChromeOS.
    let block_size = BlockSize::BS_512;

    info!("Start Flex-ing");
    copy_image_to_rootfs(device_path)?;

    info!("Setting up the disk");
    setup_disk(device_path, block_size)?;

    info!("Setting up the new partition and installing ChromeOS Flex");
    setup_flex_deploy_partition_and_install(device_path)?;

    info!("Rebooting into ChromeOS Flex, keep fingers crossed");
    reboot(nix::sys::reboot::RebootMode::RB_AUTOBOOT)?;

    Ok(())
}
