// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, bail, Result};
use fatfs::{FileSystem, FsOptions, ReadWriteSeek};
use gpt_disk_types::{BlockSize, GptPartitionType, Lba, LbaRangeInclusive};
use gptman::{GPTPartitionEntry, GPT};
use std::fs::{self, File, OpenOptions};
use std::io::{Cursor, Read, Seek, SeekFrom, Write};
use std::ops::RangeInclusive;
use std::path::{Path, PathBuf};
use std::process::Command;

const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEXOR_VMLINUZ_FILENAME: &str = "flexor_vmlinuz";

const SECTOR_SIZE: BlockSize = BlockSize::BS_512;

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

    fn num_bytes(&self) -> usize {
        self.0.num_bytes(SECTOR_SIZE).unwrap().try_into().unwrap()
    }

    fn read_bytes_from_file(&self, f: &mut File) -> Result<Vec<u8>> {
        let mut v = vec![0; self.num_bytes()];
        f.seek(SeekFrom::Start(*self.to_byte_range().start()))?;
        f.read_exact(&mut v)?;
        Ok(v)
    }

    fn write_bytes_to_file(&self, f: &mut File, data: &[u8]) -> Result<()> {
        assert!(data.len() <= self.num_bytes());
        f.seek(SeekFrom::Start(*self.to_byte_range().start()))?;
        Ok(f.write_all(data)?)
    }
}

// Find the data partition on disk (assume there's only one) and return its location & size.
fn get_data_partition_range(disk_file: &mut File) -> Result<PartitionDataRange> {
    let gpt = GPT::read_from(disk_file, SECTOR_SIZE.to_u64())?;

    let data_guid = GptPartitionType::BASIC_DATA.0.to_bytes();
    let data_partition = gpt
        .iter()
        .find(|(_, part)| part.partition_type_guid == data_guid)
        .ok_or(anyhow!("Couldn't find a data partition."))?
        .1;

    Ok(PartitionDataRange::new(data_partition))
}

fn write_to_fatfs<T: ReadWriteSeek>(
    root_dir: &fatfs::Dir<T>,
    path: &str,
    input: &Path,
) -> std::io::Result<()> {
    let data = fs::read(input)?;

    // Remove if it already exists.
    let _ = root_dir.remove(path);
    // Write out the new version.
    let mut output_file = root_dir.create_file(path)?;
    output_file.write_all(&data)
}

/// Updates a flexor test disk image, inserting a new flexor_vmlinuz and/or install_image
/// if either is passed.
pub fn update(
    flexor_disk: &Path,
    flexor_vmlinuz: &Option<PathBuf>,
    install_image: &Option<PathBuf>,
) -> Result<()> {
    if flexor_vmlinuz.is_none() && install_image.is_none() {
        // Nothing to do.
        return Ok(());
    }

    println!("Updating disk...");

    let mut disk_file = OpenOptions::new()
        .read(true)
        .write(true)
        .truncate(false)
        .open(flexor_disk)?;

    let data_partition_range = get_data_partition_range(&mut disk_file)?;
    let mut data_partition_data = data_partition_range.read_bytes_from_file(&mut disk_file)?;

    {
        // Load the data as a FAT filesystem, and grab the root dir.
        let cursor = Cursor::new(&mut data_partition_data);
        let data_fs = FileSystem::new(cursor, FsOptions::new())?;
        let root_dir = data_fs.root_dir();

        if let Some(vmlinuz_path) = flexor_vmlinuz {
            write_to_fatfs(&root_dir, FLEXOR_VMLINUZ_FILENAME, vmlinuz_path)?;
        }

        if let Some(image_path) = install_image {
            write_to_fatfs(&root_dir, FLEX_IMAGE_FILENAME, image_path)?;
        }
    }

    data_partition_range.write_bytes_to_file(&mut disk_file, &data_partition_data)
}

/// Runs a flexor test disk image.
pub fn run(flexor_disk: &Path) -> Result<()> {
    // Command copied from go/cros-frd/flexor.
    let mut cmd = Command::new("qemu-system-x86_64");
    cmd.args(["-enable-kvm", "-mem-prealloc", "-nographic"]);
    cmd.args(["-m", "16G"]);
    cmd.args(["-device", "qemu-xhci"]);
    cmd.args(["-device", "usb-tablet"]);
    cmd.args(["-rtc", "clock=host,base=localtime"]);
    cmd.args(["-display", "none"]);
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

    println!("{cmd:?}");

    let status = cmd.status()?;

    if !status.success() {
        bail!("Qemu exited incorrectly.");
    }

    Ok(())
}
