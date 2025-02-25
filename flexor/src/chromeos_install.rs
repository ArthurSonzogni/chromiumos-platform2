// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{path::Path, process::Command};

use crate::util::execute_command;
use anyhow::Result;
use libchromeos::mount::{self, FsType, Mount};

const EFIVARFS_PATH: &str = "/sys/firmware/efi/efivars";

/// Writes the GPT partition table to disk and writes the stateful partition.
pub fn write_partition_table_and_stateful(disk_path: &Path) -> Result<()> {
    let mut cmd = get_chromeos_install_cmd(disk_path);
    // We skip installing the rootfs and all other partitions beside stateful.
    cmd.arg("--skip_rootfs");
    // Give the path to the PMBR code.
    cmd.arg("--pmbr_code").arg("/usr/share/syslinux/gptmbr.bin");

    execute_command(cmd)
}

/// Mount efivarfs.
///
/// efivarfs is used in postinst to set the boot order so that Flex is the first choice for booting.
/// Some devices also don't follow the UEFI spec, and don't look in the default location for
/// bootloaders. This allows postinst to set up a boot entry so that even those devices can boot.
pub fn mount_efivarfs() -> Result<Mount> {
    // See https://docs.kernel.org/filesystems/efivarfs.html
    Ok(mount::Builder::new(Path::new("none"))
        .fs_type(FsType::Efivarfs)
        .mount(Path::new(EFIVARFS_PATH))?)
}

/// Installs an image onto a disk using the chromeos_install script.
pub fn install_image_to_disk(disk_path: &Path, image_path: &Path) -> Result<()> {
    let mut cmd = get_chromeos_install_cmd(disk_path);
    // Set the path to the image on disk.
    cmd.arg("--payload_image").arg(image_path);
    // Target BIOS is EFI.
    cmd.arg("--target_bios").arg("efi");
    // Skip writing the partition table since we already put this before.
    cmd.arg("--skip_gpt_creation");

    execute_command(cmd)
}

fn get_chromeos_install_cmd(disk_path: &Path) -> Command {
    let mut cmd = Command::new("/usr/sbin/chromeos-install");
    // Accept everything.
    cmd.arg("--yes");
    // Set the destination path.
    cmd.arg("--dst").arg(disk_path);
    // Skip sanity check for destination being removable.
    cmd.arg("--skip_dst_removable");

    cmd
}
