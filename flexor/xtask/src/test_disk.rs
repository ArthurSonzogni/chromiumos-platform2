// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file_view::FileView;
use crate::CreateArgs;
use anyhow::{anyhow, bail, Context, Result};
use fatfs::{FileSystem, FormatVolumeOptions, FsOptions, ReadWriteSeek};
use fs_err::{self as fs, File, OpenOptions};
use gpt_disk_types::{BlockSize, GptPartitionType, Lba, LbaRangeInclusive};
use gptman::{GPTPartitionEntry, GPT};
use std::collections::BTreeMap;
use std::fs::Metadata;
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

/// Convert MiB to bytes.
///
/// Panics if |mib| is too big.
fn mib_to_bytes(mib: u64) -> u64 {
    mib * 1024 * 1024
}

/// Convert GiB to bytes.
///
/// Panics if |gib| is too big.
fn gib_to_bytes(gib: u64) -> u64 {
    gib * 1024 * 1024 * 1024
}

/// Returns the smallest number of sectors that can contain the given number of bytes (rounds up).
///
/// We can safely assume a sector size of 512 when modifying these images. The 4k sector size issues
/// only arise when writing to an actual disk with a different sector size.
fn bytes_to_sectors(bytes: u64) -> u64 {
    bytes.div_ceil(SECTOR_SIZE.to_u64())
}

/// Round |bytes| up to the nearest sector boundary.
///
/// Like |bytes_to_sectors| but returns a number of bytes, not number of sectors.
fn round_bytes_to_nearest_sector(bytes: u64) -> u64 {
    bytes.div_ceil(SECTOR_SIZE.to_u64()) * SECTOR_SIZE.to_u64()
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

type FileNameToSrcPathMap = BTreeMap<&'static str, PathBuf>;

/// Add up the sizes of all the files (values in the passed map).
fn sum_sizes(files: &FileNameToSrcPathMap) -> Result<u64> {
    let size = files
        .values()
        .map(fs::metadata)
        // collapse to handle errors, then iterate on the metadata again.
        .collect::<Result<Vec<Metadata>, std::io::Error>>()?
        .iter()
        .map(Metadata::len)
        .sum();

    Ok(size)
}

/// Represents one of the partitions we'd like to make.
struct Partition {
    gpt_type: GptPartitionType,
    /// All the files go in the same subdir, because that's all we need right now.
    subdir: PathBuf,
    /// Files will be named for the keys in the |name_to_src_path|, and their contents will be
    /// pulled from the files pointed to by the values.
    name_to_src_path: FileNameToSrcPathMap,
    /// When producing minimal-sized partitions this is a "fudge factor" for sizing the partition.
    /// This covers space reserved for optional files and filesystem overhead. FAT filesystem
    /// overhead is tricky to calculate, and depends on the initial settings, number of files,
    /// file alignment, and fragmentation.
    extra_space: u64,
}

impl Partition {
    fn required_size_in_bytes(&self) -> Result<u64> {
        Ok(sum_sizes(&self.name_to_src_path)? + self.extra_space)
    }
}

/// Plan out what's going to be on the esp.
///
/// Includes crdyshim, crdyboot + signature, and an optional crdyboot_verbose file.
fn plan_esp_partition(
    frd_bundle: &Path,
    crdyshim: &Option<PathBuf>,
    crdyboot: &Option<PathBuf>,
    crdyboot_verbose: bool,
) -> Partition {
    let frd_bundle_install_dir = frd_bundle.join(FRD_BUNDLE_INSTALL_DIR);

    let crdyshim = crdyshim
        .clone()
        .unwrap_or_else(|| frd_bundle_install_dir.join(CRDYSHIM_FILENAME));
    let crdyboot = crdyboot
        .clone()
        .unwrap_or_else(|| frd_bundle_install_dir.join(CRDYBOOT_FILENAME));
    let crdyboot_sig = crdyboot.with_extension("sig");
    let mut esp_files = FileNameToSrcPathMap::from([
        (CRDYSHIM_FILENAME, crdyshim),
        (CRDYBOOT_FILENAME, crdyboot),
        (CRDYBOOT_SIG_FILENAME, crdyboot_sig),
    ]);

    if crdyboot_verbose {
        esp_files.insert(
            CRDYBOOT_VERBOSE_FILENAME,
            // Will produce a 0-length file, which is all we need for crdyboot_verbose.
            PathBuf::from("/dev/null"),
        );
    }

    Partition {
        gpt_type: GptPartitionType::EFI_SYSTEM,
        subdir: PathBuf::from("EFI/BOOT"),
        name_to_src_path: esp_files,
        // This seems to be more than enough to cover FAT overhead on both base and test images.
        extra_space: mib_to_bytes(1),
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
    let mut install_files = FileNameToSrcPathMap::from([
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
        // With a test image we seem to need close to 8MiB for FAT overhead. Plus leave space for
        // the optional config.json, in case `package_flex_image` uses that file to pass the
        // enrollment token.
        extra_space: mib_to_bytes(10),
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

/// Create a correctly partitioned disk image.
fn create_image(path: &Path, esp_size: u64, data_size: u64) -> Result<()> {
    // Delete the output file if it already exists.
    let _ = fs_err::remove_file(path);

    // Don't pass exabyte-large files: this might overflow.
    let total_size_bytes = esp_size + data_size;
    // `sgdisk` complains if the file isn't a multiple of the sector size, so round up.
    let total_size_bytes = round_bytes_to_nearest_sector(total_size_bytes);
    // `sgdisk` doesn't seem to accept bytes, convert to sectors.
    let esp_size_sectors = bytes_to_sectors(esp_size);

    // Create empty file to hold the disk.
    run_cmd(
        Command::new("truncate")
            .arg(format!("--size={total_size_bytes}"))
            .arg(path),
    )?;

    run_cmd(
        Command::new("sgdisk")
            // Specify explicitly, because we've run into issues before when assuming.
            .arg(format!("--set-alignment={SECTOR_SIZE}"))
            // Create ESP partition.
            .args([
                &format!("--new=1::+{esp_size_sectors}"),
                &format!("--typecode=1:{}", GptPartitionType::EFI_SYSTEM),
            ])
            // Create flexor data partition.
            .args([
                // No need to specify size, this will use the remainder.
                "--new=2",
                &format!("--typecode=2:{}", GptPartitionType::BASIC_DATA),
            ])
            .arg(path),
    )?;

    Ok(())
}

/// Create a flexor disk from scratch.
pub fn create(args: &CreateArgs) -> Result<()> {
    let esp_part = plan_esp_partition(
        &args.frd_bundle,
        &args.crdyshim,
        &args.crdyboot,
        args.crdyboot_verbose,
    );
    let data_part =
        plan_data_partition(&args.frd_bundle, &args.flexor_vmlinuz, &args.install_image);

    if args.mass_deployable {
        let esp_size = esp_part.required_size_in_bytes()?;
        let data_size = data_part.required_size_in_bytes()?;

        create_image(&args.output, esp_size, data_size)?;
    } else {
        let esp_size = mib_to_bytes(90);
        let data_size = gib_to_bytes(30); // 30GiB (FRD claims to require 24GiB)

        create_image(&args.output, esp_size, data_size)?;
    }

    let mut disk_file = OpenOptions::new()
        .read(true)
        .write(true)
        .truncate(false)
        .open(&args.output)?;

    create_partition(&mut disk_file, &esp_part).context("failed to create esp")?;
    create_partition(&mut disk_file, &data_part).context("failed to create data partition")?;

    Ok(())
}

/// Run a flexor disk image.
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
    fn test_sum_sizes() -> Result<()> {
        let tmp_dir = tempfile::tempdir()?;

        // Make some files with a clear size.
        let file_a = tmp_dir.path().join("file_a");
        fs_err::write(&file_a, "aaa")?;
        let file_b = tmp_dir.path().join("file_b");
        fs_err::write(&file_b, "bbb")?;

        // An empty set of files should take up no space.
        assert_eq!(sum_sizes(&FileNameToSrcPathMap::new())?, 0);
        // One file is counted right.
        assert_eq!(
            sum_sizes(&FileNameToSrcPathMap::from([("a", file_a.clone())]))?,
            3
        );
        // Two files are correctly added.
        assert_eq!(
            sum_sizes(&FileNameToSrcPathMap::from([
                ("a", file_a.clone()),
                ("b", file_b.clone())
            ]))?,
            6
        );
        // Reusing the same path still counts twice.
        assert_eq!(
            sum_sizes(&FileNameToSrcPathMap::from([
                ("a", file_a.clone()),
                ("b", file_b.clone()),
                ("x", file_b.clone())
            ]))?,
            9
        );

        Ok(())
    }

    #[test]
    fn test_bytes_to_sectors() {
        for (input, expected_result) in [
            (0, 0),
            (1, 1),
            (512, 1),
            (513, 2),
            (2048, 4),
            (51200, 100),
            (51201, 101),
            // 100 GiB. Likely larger than what we're going to use it for here.
            (107374182400, 209715200),
        ] {
            assert_eq!(bytes_to_sectors(input), expected_result);
        }
    }

    #[test]
    fn test_round_bytes_to_nearest_sector() {
        for (input, expected_result) in [
            (0, 0),
            (1, 512),
            (512, 512),
            (513, 1024),
            (2048, 2048),
            (51200, 51200),
            (51201, 51712),
            (gib_to_bytes(100), gib_to_bytes(100)),
        ] {
            assert_eq!(round_bytes_to_nearest_sector(input), expected_result);
        }
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
            name_to_src_path: FileNameToSrcPathMap::from([
                ("file_a", file_a),
                ("different_name", file_b),
            ]),
            extra_space: mib_to_bytes(1),
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
        // Test 'defaults'.
        let partition = plan_esp_partition(Path::new("fake"), &None, &None, false);

        assert_eq!(partition.gpt_type, GptPartitionType::EFI_SYSTEM);
        assert_eq!(partition.subdir, PathBuf::from("EFI/BOOT"));
        assert_eq!(
            partition.name_to_src_path,
            FileNameToSrcPathMap::from([
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

        // Check turning on crdyboot_verbose.
        let partition = plan_esp_partition(Path::new("fake"), &None, &None, true);
        assert_eq!(
            partition.name_to_src_path,
            FileNameToSrcPathMap::from([
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
                (CRDYBOOT_VERBOSE_FILENAME, PathBuf::from("/dev/null")),
            ]),
        );

        // Check overriding.
        let partition = plan_esp_partition(
            Path::new("fake"),
            &Some(PathBuf::from("shim.efi")),
            &Some(PathBuf::from("boot.efi")),
            false,
        );
        assert_eq!(
            partition.name_to_src_path,
            FileNameToSrcPathMap::from([
                (CRDYSHIM_FILENAME, PathBuf::from("shim.efi")),
                (CRDYBOOT_FILENAME, PathBuf::from("boot.efi")),
                (CRDYBOOT_SIG_FILENAME, PathBuf::from("boot.sig")),
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
            FileNameToSrcPathMap::from([
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
            FileNameToSrcPathMap::from([
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
            FileNameToSrcPathMap::from([
                (FLEXOR_VMLINUZ_FILENAME, PathBuf::from("vmlinuz")),
                (FLEX_IMAGE_FILENAME, PathBuf::from("image")),
            ]),
        );
    }
}
