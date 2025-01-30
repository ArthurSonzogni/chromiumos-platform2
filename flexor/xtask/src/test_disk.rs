// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file_view::FileView;
use crate::{CreateTestDisk, TestDiskArgs};
use anyhow::{anyhow, bail, Context, Result};
use fatfs::{FileSystem, FormatVolumeOptions, FsOptions, ReadWriteSeek};
use fs_err::{File, OpenOptions};
use gpt_disk_types::{BlockSize, GptPartitionType, Lba, LbaRangeInclusive};
use gptman::{GPTPartitionEntry, GPT};
use std::io::Write;
use std::ops::RangeInclusive;
use std::path::Path;
use std::process::Command;

const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEXOR_VMLINUZ_FILENAME: &str = "flexor_vmlinuz";

const SECTOR_SIZE: BlockSize = BlockSize::BS_512;

/// Convert a `Command` to a `String` for display.
///
/// This is not a precise conversion, but sufficient for logging.
fn cmd_to_string(cmd: &Command) -> String {
    format!("{cmd:?}").replace('"', "")
}

/// Run a command.
///
/// This prints the command before running it.
///
/// # Errors
///
/// An error is returned if the command fails to launch or exits non-zero.
fn run_cmd(cmd: &mut Command) -> Result<()> {
    let cmd_str = cmd_to_string(cmd);

    println!("{cmd_str}");

    let status = cmd
        .status()
        .with_context(|| format!("failed to launch command: \"{cmd_str}\""))?;
    if !status.success() {
        bail!("command \"{cmd_str}\" exited non-zero: {status}");
    }

    Ok(())
}

// Copied from crdyboot
struct PartitionDataRange(LbaRangeInclusive);

impl PartitionDataRange {
    fn new(partition: &GPTPartitionEntry) -> Self {
        Self(
            LbaRangeInclusive::new(Lba(partition.starting_lba), Lba(partition.ending_lba)).unwrap(),
        )
    }

    fn to_byte_range(&self) -> RangeInclusive<u64> {
        self.0.to_byte_range(SECTOR_SIZE).unwrap()
    }
}

/// Find a partition in `disk_file` by partition type.
///
/// Partitions are search in order and the first matching partition is
/// returned. An error is returned if there is no matching partition.
fn find_partition_range(
    disk_file: &mut File,
    partition_type: GptPartitionType,
) -> Result<PartitionDataRange> {
    let gpt = GPT::read_from(disk_file, SECTOR_SIZE.to_u64())?;

    let guid = partition_type.0.to_bytes();
    let data_partition = gpt
        .iter()
        .find(|(_, part)| part.partition_type_guid == guid)
        .ok_or(anyhow!("no partition of type {partition_type}"))?
        .1;

    Ok(PartitionDataRange::new(data_partition))
}

fn write_to_fatfs<T: ReadWriteSeek>(
    root_dir: &fatfs::Dir<T>,
    path: &str,
    input: &Path,
) -> std::io::Result<()> {
    let data = fs_err::read(input)?;

    // Remove if it already exists.
    let _ = root_dir.remove(path);
    // Write out the new version.
    let mut output_file = root_dir.create_file(path)?;
    output_file.write_all(&data)
}

/// Create the ESP filesystem in a flexor test disk.
///
/// Bootloader executables and signature are copied to the filesystem.
fn create_esp(args: &CreateTestDisk, disk_file: &mut File) -> Result<()> {
    // Create empty filesystem.
    let partition_range = find_partition_range(disk_file, GptPartitionType::EFI_SYSTEM)
        .context("failed to get esp partition range")?;
    let mut view = FileView::new(disk_file, partition_range.to_byte_range())?;
    fatfs::format_volume(&mut view, FormatVolumeOptions::new())?;

    // Add files to the filesystem.
    let files_to_copy = ["bootx64.efi", "crdybootx64.efi", "crdybootx64.sig"];
    let fs = FileSystem::new(view, FsOptions::new())?;
    let root = fs.root_dir();
    let boot = root.create_dir("EFI")?.create_dir("BOOT")?;
    for file_name in files_to_copy {
        write_to_fatfs(
            &boot,
            file_name,
            &args.frd_bundle.join("install").join(file_name),
        )?;
    }

    Ok(())
}

/// Create the data partition in a flexor test disk.
///
/// The flexor kernel and installation image are copied to the filesystem.
fn create_flexor_data_partition(args: &CreateTestDisk, disk_file: &mut File) -> Result<()> {
    // Create the flexor data partition
    let partition_range = find_partition_range(disk_file, GptPartitionType::BASIC_DATA)
        .context("failed to get flexor data partition range")?;
    let mut view = FileView::new(disk_file, partition_range.to_byte_range())?;
    fatfs::format_volume(&mut view, FormatVolumeOptions::new())?;

    // Add files to the filesystem.
    let files_to_copy = ["flex_image.tar.xz", "flexor_vmlinuz"];
    let fs = FileSystem::new(view, FsOptions::new())?;
    let root = fs.root_dir();
    for file_name in files_to_copy {
        write_to_fatfs(
            &root,
            file_name,
            &args.frd_bundle.join("install").join(file_name),
        )?;
    }

    Ok(())
}

/// Create a flexor test disk from scratch.
pub fn create(args: &CreateTestDisk) -> Result<()> {
    // Delete the output file if it already exists.
    let _ = fs_err::remove_file(&args.output);

    // Create empty file to hold the disk.
    run_cmd(
        Command::new("truncate")
            .arg("--size=32GiB")
            .arg(&args.output),
    )?;

    run_cmd(
        Command::new("sgdisk")
            // Create ESP partition.
            .args([
                "--new=1::+90MB",
                &format!("--typecode=1:{}", GptPartitionType::EFI_SYSTEM),
            ])
            // Create flexor data partition.
            .args([
                "--new=2",
                &format!("--typecode=2:{}", GptPartitionType::BASIC_DATA),
            ])
            .arg(&args.output),
    )?;

    let mut disk_file = OpenOptions::new()
        .read(true)
        .write(true)
        .truncate(false)
        .open(&args.output)?;

    create_esp(args, &mut disk_file).context("failed to create esp")?;
    create_flexor_data_partition(args, &mut disk_file)
        .context("failed to create flexor data partition")?;

    Ok(())
}

/// Updates a flexor test disk image, inserting a new flexor_vmlinuz and/or install_image
/// if either is passed.
pub fn update(args: &TestDiskArgs) -> Result<()> {
    if args.flexor_vmlinuz.is_none() && args.install_image.is_none() {
        // Nothing to do.
        println!("No files passed, not updating disk...");
        return Ok(());
    } else {
        println!("Updating disk...");
    }

    let mut disk_file = OpenOptions::new()
        .read(true)
        .write(true)
        .truncate(false)
        .open(&args.flexor_disk)?;

    let data_partition_range = find_partition_range(&mut disk_file, GptPartitionType::BASIC_DATA)?;

    let view = FileView::new(&mut disk_file, data_partition_range.to_byte_range())?;

    // Load the data as a FAT filesystem, and grab the root dir.
    let data_fs = FileSystem::new(view, FsOptions::new())?;
    let root_dir = data_fs.root_dir();

    if let Some(vmlinuz_path) = &args.flexor_vmlinuz {
        write_to_fatfs(&root_dir, FLEXOR_VMLINUZ_FILENAME, vmlinuz_path)?;
    }

    if let Some(image_path) = &args.install_image {
        write_to_fatfs(&root_dir, FLEX_IMAGE_FILENAME, image_path)?;
    }

    Ok(())
}

/// Runs a flexor test disk image.
pub fn run(flexor_disk: &Path) -> Result<()> {
    let mut cmd = Command::new("qemu-system-x86_64");
    cmd.args(["-enable-kvm"]);
    cmd.args(["-m", "8G"]);
    cmd.args(["-device", "qemu-xhci"]);
    cmd.args(["-device", "usb-tablet"]);
    cmd.args(["-rtc", "clock=host,base=localtime"]);
    cmd.args(["-display", "sdl"]);
    cmd.args(["-vga", "virtio"]);
    cmd.args(["-net", "user,hostfwd=tcp::10022-:22"]);
    cmd.args(["-net", "nic"]);
    cmd.args([
        "-chardev",
        "stdio,id=char0,mux=on,signal=on,logfile=flexor.log",
    ]);
    cmd.args(["-serial", "chardev:char0"]);
    cmd.args(["-mon", "chardev=char0"]);
    cmd.args(["-parallel", "none"]);
    cmd.args([
        "-drive",
        "if=pflash,format=raw,readonly=on,file=/usr/share/ovmf/OVMF.fd",
    ]);
    cmd.args([
        "-drive",
        &format!("format=raw,file={}", flexor_disk.display()),
    ]);

    run_cmd(&mut cmd)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cmd_to_string() {
        assert_eq!(
            cmd_to_string(Command::new("prog").args(["--arg1", "arg2"])),
            "prog --arg1 arg2"
        );
    }

    #[test]
    fn test_run_cmd() {
        // Error: fails to launch.
        assert!(run_cmd(&mut Command::new("this-command-does-not-exist")).is_err());

        // Error: exits non-zero.
        assert!(run_cmd(&mut Command::new("false")).is_err());

        // Success.
        assert!(run_cmd(&mut Command::new("true")).is_ok());
    }
}
