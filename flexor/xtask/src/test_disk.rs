// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file_view::FileView;
use crate::CreateArgs;
use anyhow::{anyhow, bail, Context, Result};
use fatfs::{FileSystem, FormatVolumeOptions, FsOptions, ReadWriteSeek};
use fs_err::{File, OpenOptions};
use gpt_disk_types::{BlockSize, GptPartitionType, Lba, LbaRangeInclusive};
use gptman::{GPTPartitionEntry, GPT};
use std::collections::BTreeMap;
use std::io::Write;
use std::ops::RangeInclusive;
use std::path::{Path, PathBuf};
use std::process::Command;

const CRDYBOOT_FILENAME: &str = "crdybootx64.efi";
const CRDYBOOT_SIG_FILENAME: &str = "crdybootx64.sig";
const CRDYBOOT_VERBOSE_FILENAME: &str = "crdyboot_verbose";
const CRDYSHIM_FILENAME: &str = "bootx64.efi";
const FLEX_CONFIG_FILENAME: &str = "flex_config.json";
const FLEX_IMAGE_FILENAME: &str = "flex_image.tar.xz";
const FLEXOR_VMLINUZ_FILENAME: &str = "flexor_vmlinuz";
const FRD_BUNDLE_INSTALL_DIR: &str = "install";

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

/// Represents one of the partitions we'd like to make.
struct Partition {
    gpt_type: GptPartitionType,
    /// All the files go in the same subdir, because that's all we need right now.
    subdir: PathBuf,
    /// Files will be named for the keys in the |name_to_src_path|, and their contents will be
    /// pulled from the files pointed to by the values.
    name_to_src_path: BTreeMap<&'static str, PathBuf>,
}

/// Plan out what's going to be on the esp.
///
/// Includes crdyshim, crdyboot + signature, and an optional crdyboot_verbose file.
fn plan_esp_partition(frd_bundle: &Path) -> Partition {
    let frd_bundle_install_dir = frd_bundle.join(FRD_BUNDLE_INSTALL_DIR);

    let mut esp_files: BTreeMap<_, _> =
        [CRDYSHIM_FILENAME, CRDYBOOT_FILENAME, CRDYBOOT_SIG_FILENAME]
            .iter()
            .map(|name| (*name, frd_bundle_install_dir.join(name)))
            .collect();

    // Also grab crdyboot_verbose if it's there.
    let crdyboot_verbose = frd_bundle_install_dir.join(CRDYBOOT_VERBOSE_FILENAME);
    if crdyboot_verbose.exists() {
        esp_files.insert(
            CRDYBOOT_VERBOSE_FILENAME,
            frd_bundle_install_dir.join(CRDYBOOT_VERBOSE_FILENAME),
        );
    }

    Partition {
        gpt_type: GptPartitionType::EFI_SYSTEM,
        subdir: PathBuf::from("EFI/BOOT"),
        name_to_src_path: esp_files,
    }
}

/// Plan out what's going to be on the data partition.
///
/// Includes the flexor kernel+initramfs (flexor_vmlinuz), the compressed flex image to be installed
/// (flex_image.tar.xz), and an optional enrollment/onc/oobe config (flex_config.json).
fn plan_data_partition(
    frd_bundle: &Path,
    flexor_vmlinuz: &Option<PathBuf>,
    install_image: &Option<PathBuf>,
) -> Partition {
    let frd_bundle_install_dir = frd_bundle.join(FRD_BUNDLE_INSTALL_DIR);

    let flexor_vmlinuz = flexor_vmlinuz
        .clone()
        .unwrap_or_else(|| frd_bundle_install_dir.join(FLEXOR_VMLINUZ_FILENAME));
    let install_image = install_image
        .clone()
        .unwrap_or_else(|| frd_bundle_install_dir.join(FLEX_IMAGE_FILENAME));
    let mut install_files = BTreeMap::from([
        (FLEXOR_VMLINUZ_FILENAME, flexor_vmlinuz),
        (FLEX_IMAGE_FILENAME, install_image),
    ]);

    // Also grab flex_config.json if it's there.
    // This is a little different from running under the agent, which would create the config rather
    // than grabbing it from the install dir.
    let flex_config = frd_bundle_install_dir.join(FLEX_CONFIG_FILENAME);
    if flex_config.exists() {
        install_files.insert(FLEX_CONFIG_FILENAME, flex_config);
    }

    Partition {
        gpt_type: GptPartitionType::BASIC_DATA,
        subdir: PathBuf::new(),
        name_to_src_path: install_files,
    }
}

/// Create a partition on the given |disk_file| based on the details in |partition|.
fn create_partition(disk_file: &mut File, partition: &Partition) -> Result<()> {
    // Create empty filesystem.
    let partition_range = find_partition_range(disk_file, partition.gpt_type)
        .context("failed to get partition range")?;
    let mut view = FileView::new(disk_file, partition_range.to_byte_range())?;
    fatfs::format_volume(&mut view, FormatVolumeOptions::new())?;

    let fs = FileSystem::new(view, FsOptions::new())?;
    let mut root = fs.root_dir();
    for dir in &partition.subdir {
        // OK to unwrap: We control the path that is passed in, and it doesn't contain unicode.
        let dir = dir.to_str().unwrap();
        root = root.create_dir(dir)?;
    }
    for (file_name, path) in &partition.name_to_src_path {
        write_to_fatfs(&root, file_name, path)?;
    }
    Ok(())
}

/// Create a flexor test disk from scratch.
pub fn create(args: &CreateArgs) -> Result<()> {
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

    let esp_part = plan_esp_partition(&args.frd_bundle);
    let data_part =
        plan_data_partition(&args.frd_bundle, &args.flexor_vmlinuz, &args.install_image);

    create_partition(&mut disk_file, &esp_part).context("failed to create esp")?;
    create_partition(&mut disk_file, &data_part).context("failed to create data partition")?;

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
    use std::io;

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

    #[test]
    fn test_create_partition() -> Result<()> {
        let tmp_dir = tempfile::tempdir()?;

        let disk = tmp_dir.path().join("disk");

        // Create empty disk.
        run_cmd(Command::new("truncate").arg("--size=1GiB").arg(&disk))?;
        run_cmd(
            Command::new("sgdisk")
                .args([
                    "--new=1",
                    &format!("--typecode=1:{}", GptPartitionType::BASIC_DATA),
                ])
                .arg(&disk),
        )?;

        // Make some files to put on disk.
        let file_a = tmp_dir.path().join("file_a");
        fs_err::write(&file_a, "aaa")?;
        let file_b = tmp_dir.path().join("file_b");
        fs_err::write(&file_b, "bbb")?;

        // We'll use this for both writing and then reading back to check if it worked.
        let mut disk_file = OpenOptions::new()
            .read(true)
            .write(true)
            .truncate(false)
            .open(&disk)?;

        // Put a couple of files two dirs down.
        let partition = Partition {
            gpt_type: GptPartitionType::BASIC_DATA,
            subdir: PathBuf::from("one/two"),
            name_to_src_path: BTreeMap::from([("file_a", file_a), ("different_name", file_b)]),
        };
        create_partition(&mut disk_file, &partition)?;

        // Now we read it back.
        let partition_range = find_partition_range(&mut disk_file, GptPartitionType::BASIC_DATA)?;
        let view = FileView::new(&mut disk_file, partition_range.to_byte_range())?;
        let fs = FileSystem::new(view, FsOptions::new())?;
        let root = fs.root_dir();

        assert_eq!(
            io::read_to_string(root.open_file("one/two/file_a")?)?,
            "aaa"
        );
        assert_eq!(
            io::read_to_string(root.open_file("one/two/different_name")?)?,
            "bbb"
        );

        Ok(())
    }

    #[test]
    fn test_plan_esp() {
        let partition = plan_esp_partition(Path::new("fake"));

        assert_eq!(partition.gpt_type, GptPartitionType::EFI_SYSTEM);
        assert_eq!(partition.subdir, PathBuf::from("EFI/BOOT"));
        assert_eq!(
            partition.name_to_src_path,
            BTreeMap::from([
                (
                    CRDYSHIM_FILENAME,
                    Path::new("fake/install/").join(CRDYSHIM_FILENAME),
                ),
                (
                    CRDYBOOT_FILENAME,
                    Path::new("fake/install/").join(CRDYBOOT_FILENAME),
                ),
                (
                    CRDYBOOT_SIG_FILENAME,
                    Path::new("fake/install/").join(CRDYBOOT_SIG_FILENAME),
                ),
            ]),
        );
    }

    #[test]
    fn test_plan_data() {
        // Test 'defaults'.
        let partition = plan_data_partition(Path::new("fake"), &None, &None);

        assert_eq!(partition.gpt_type, GptPartitionType::BASIC_DATA);
        assert_eq!(partition.subdir, PathBuf::new());
        assert_eq!(
            partition.name_to_src_path,
            BTreeMap::from([
                (
                    FLEXOR_VMLINUZ_FILENAME,
                    Path::new("fake/install/").join(FLEXOR_VMLINUZ_FILENAME),
                ),
                (
                    FLEX_IMAGE_FILENAME,
                    Path::new("fake/install/").join(FLEX_IMAGE_FILENAME),
                )
            ]),
        );

        // Test with only one overridden.
        let partition = plan_data_partition(
            Path::new("fake"),
            &Some(PathBuf::from("different_name")),
            &None,
        );
        assert_eq!(
            partition.name_to_src_path,
            BTreeMap::from([
                (FLEXOR_VMLINUZ_FILENAME, PathBuf::from("different_name"),),
                (
                    FLEX_IMAGE_FILENAME,
                    Path::new("fake/install/").join(FLEX_IMAGE_FILENAME),
                ),
            ]),
        );

        // Test with both overridden.
        let partition = plan_data_partition(
            Path::new("fake"),
            &Some(PathBuf::from("vmlinuz")),
            &Some(PathBuf::from("image")),
        );
        assert_eq!(
            partition.name_to_src_path,
            BTreeMap::from([
                (FLEXOR_VMLINUZ_FILENAME, PathBuf::from("vmlinuz")),
                (FLEX_IMAGE_FILENAME, PathBuf::from("image")),
            ]),
        );
    }
}
