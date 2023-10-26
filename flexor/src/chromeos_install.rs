// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{path::Path, process::Command};

use crate::util::execute_command;
use anyhow::Result;

// Writes the GPT partition table to disk and writes the stateful partition.
pub fn write_partition_table_and_stateful<P: AsRef<Path>>(dst: P) -> Result<()> {
    let mut cmd = get_chromeos_install_cmd(dst);
    // We skip installing the rootfs and all other partitions beside stateful.
    cmd.arg("--skip_rootfs");
    // Give the path to the PMBR code.
    cmd.arg("--pmbr_code").arg("/usr/share/syslinux/gptmbr.bin");

    execute_command(cmd)
}

// Installs an image onto a disk using the chromeos_install script.
pub fn install_image_to_disk<P: AsRef<Path>>(dst: P, image_path: P) -> Result<()> {
    let mut cmd = get_chromeos_install_cmd(dst);
    // Set the path to the image on disk.
    cmd.arg("--payload_image").arg(image_path.as_ref());
    // Target BIOS is EFI.
    cmd.arg("--target_bios").arg("efi");
    // Skip writing the partition table since we already put this before.
    cmd.arg("--skip_writing_part_table");

    execute_command(cmd)
}

fn get_chromeos_install_cmd<P: AsRef<Path>>(dst: P) -> Command {
    let mut cmd = Command::new("/usr/sbin/chromeos-install");
    // Accept everything.
    cmd.arg("--yes");
    // Set the destination path.
    cmd.arg("--dst").arg(dst.as_ref());
    // Skip all sanity checks for source or destination being removable.
    cmd.arg("--skip_dst_removable");
    cmd.arg("--skip_src_removable");

    cmd
}
