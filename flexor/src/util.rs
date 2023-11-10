// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cgpt;
use crate::gpt::Gpt;

use anyhow::{anyhow, bail, Context, Result};
use gpt_disk_types::{BlockSize, GptPartitionEntry, Lba, LbaRangeInclusive};
use log::{error, info};
use std::{
    fs::File,
    io::BufReader,
    os::unix::ffi::OsStrExt,
    path::{Path, PathBuf},
    process::Command,
    str::from_utf8,
};
use tar::Archive;
use xz2::bufread::XzDecoder;

// Executes a command and logs its result. There are three outcomes when
// executing a command:
// 1. Everything is fine, executing the command returns exit code zero.
// 2. The command is not found and can thus not be executed.
// 3. The command is found and executed, but returns a non-zero exit code.
// The returned [`Result`] from this function maps 1. to `Ok` and 2., 3.
// To the `Err`` case.
pub fn execute_command(mut command: Command) -> Result<()> {
    info!("Executing command: {:?}", command);

    match command.status() {
        Ok(status) if status.success() => {
            info!("Executed command succesfully; omitting logs.");
            Ok(())
        }
        Err(err) => {
            error!("Executed command failed: {err}");
            Err(anyhow!("Unable to execute command: {err}"))
        }
        Ok(status) => {
            let status_code = status.code().unwrap_or(-1);
            error!("Executed command failed:  Got error status code: {status_code}",);

            let output = command.output().context("Unable to collect logs.")?;
            let stdout = from_utf8(&output.stdout).context("Unable to collect logs.")?;
            let stderr = from_utf8(&output.stderr).context("Unable to collect logs.")?;

            error!(
                "Logs of the failing command: {}",
                &format!("stdout: {}; stderr: {}", stdout, stderr,)
            );
            Err(anyhow!("Got bad status code: {status_code}"))
        }
    }
}

/// Reload the partition table on block devices.
pub fn reload_partitions(device: &Path) -> Result<()> {
    // In some cases, we may be racing with udev for access to the
    // device leading to EBUSY when we reread the partition table.  We
    // avoid the conflict by using `udevadm settle`, so that udev goes
    // first.
    let mut settle_cmd = Command::new("udevadm");
    settle_cmd.arg("settle");
    execute_command(settle_cmd)?;

    // Now we re-read the partition table using `blockdev`.
    let mut blockdev_cmd = Command::new("/sbin/blockdev");
    blockdev_cmd.arg("--rereadpt").arg(device);

    execute_command(blockdev_cmd)
}

/// Uncompresses a tar from `src` to `dst`. In this case `src` needs to point to
/// a tar archive and `dst` to a folder where the items are unpacked to. This
/// also returns an `Vec<PathBuf>` of the entries that have been successfully
/// unpacked to `dst`. Please note that these paths are relative to `dst´.
pub fn uncompress_tar_xz<P1: AsRef<Path>, P2: AsRef<Path>>(
    src: P1,
    dst: P2,
) -> Result<Vec<PathBuf>> {
    let file = File::open(src).context("Unable to open tar archive")?;
    let xz_decoder = XzDecoder::new(BufReader::new(file));

    let mut result: Vec<PathBuf> = vec![];
    let mut archive = Archive::new(xz_decoder);
    for entry in archive
        .entries()
        .context("Unable to access all contents of the tar")?
    {
        let mut entry = entry.context("Unable to read entry of the tar")?;
        entry
            .unpack_in(dst.as_ref())
            .context("Unable to unpack entry of the tar")?;

        result.push(
            entry
                .path()
                .context("Unable to get tar entries path")?
                .to_path_buf(),
        );
    }

    Ok(result)
}

/// Creates an EXT4 filesystem on `device`.
pub fn mkfs_ext4(device: &Path) -> Result<()> {
    // We use the mkfs.ext4 binary to put the filesystem.
    let mut cmd = Command::new("mkfs.ext4");
    cmd.arg(device);

    execute_command(cmd)
}

const NEW_PARTITION_SIZE_BYTES: u64 = 8_000_000_000;
const NEW_PARTITION_NAME: &str = "FLEX_DEPLOY";

/// Inserts a thirtheenth partition after the stateful partition (shrinks
/// stateful partition). This can only be called with a disk that already
/// has a ChromeOS partition layout. Since this method is just changing
/// the partition layout but not the filesystem, it assumes the filesystem
/// on stateful partition will be re-created later.
pub fn insert_thirteenth_partition(disk_path: &Path, block_size: BlockSize) -> Result<()> {
    let mut file = File::open(disk_path)?;
    let mut gpt = Gpt::from_file(&mut file, block_size)?;

    let new_part_size_lba = NEW_PARTITION_SIZE_BYTES / block_size.to_u64();
    let stateful_name = "STATE";

    let current_stateful = gpt
        .get_entry_for_partition_with_label(stateful_name.parse().unwrap())?
        .context("Unable to locate stateful partition on disk")?;

    let new_stateful_range = shrink_partition_by(current_stateful, new_part_size_lba)
        .context("Unable to shrink stateful partiton")?;
    cgpt::resize_cgpt_partition(1, disk_path, stateful_name, new_stateful_range)?;

    let new_range = add_partition_after(new_stateful_range, new_part_size_lba)
        .context("Unable to calculate new partition range")?;
    cgpt::add_cgpt_partition(13, disk_path, NEW_PARTITION_NAME, new_range)
}

fn shrink_partition_by(
    part_info: GptPartitionEntry,
    size_in_lba: u64,
) -> Result<LbaRangeInclusive> {
    // Add one because the range is "inclusive".
    let current_part_size = part_info.ending_lba.to_u64() - part_info.starting_lba.to_u64() + 1;
    if current_part_size < size_in_lba {
        bail!(
            "Can't make place for a new partition with size {size_in_lba} if the current
             partition only has size {current_part_size}"
        );
    }

    let curr_part_new_size = current_part_size - size_in_lba;
    // Subtract one from the size because the range is "inclusive".
    let new_range = LbaRangeInclusive::new(
        Lba(part_info.starting_lba.to_u64()),
        Lba(part_info.starting_lba.to_u64() + curr_part_new_size - 1),
    )
    .context("Error calculating partition range")?;

    Ok(new_range)
}

fn add_partition_after(range: LbaRangeInclusive, size_in_lba: u64) -> Result<LbaRangeInclusive> {
    let new_part_range = LbaRangeInclusive::new(
        Lba(range.end().to_u64() + 1),
        Lba(range.end().to_u64() + size_in_lba),
    )
    .context("Error calculating partition range")?;

    Ok(new_part_range)
}

#[cfg(test)]
mod tests {
    use super::*;
    use gpt_disk_types::LbaLe;

    const FILE_CONTENTS: &[u8] = b"Hello World!";
    const FILE_NAME: &str = "foo.txt";
    const TAR_NAME: &str = "foo.tar.xz";

    fn setup_tar_xz() -> Result<tempfile::TempDir> {
        // First setup a tempdir.
        let tempdir = tempfile::tempdir()?;

        // Next create a file in there.
        let file_path = tempdir.path().join(FILE_NAME);
        std::fs::write(file_path, FILE_CONTENTS)?;

        // Now create a tar.xz of that file.
        let mut tar_cmd = Command::new("tar");
        // Tell tar to (c)ompress an xz (J) of a (f)ile.
        tar_cmd.arg("-cJf").arg(tempdir.path().join(TAR_NAME));
        // Change dir to the temp path so that that the file is added
        // without a directory prefix.
        tar_cmd.arg("-C");
        tar_cmd.arg(tempdir.path());
        // We want to compress the newly created file.
        tar_cmd.arg(FILE_NAME);
        execute_command(tar_cmd)?;

        Ok(tempdir)
    }

    #[test]
    fn test_uncompress_tar_xz() -> Result<()> {
        let tempdir = setup_tar_xz()?;
        // Create a new dir where we uncompress to.
        let new_dir_path = tempdir.path().join("uncompressed");
        std::fs::create_dir(&new_dir_path)?;

        // Uncompress the file.
        let file_path = tempdir.path().join(TAR_NAME);
        let result = uncompress_tar_xz(file_path, &new_dir_path)?;

        // Compare for equality.
        assert_eq!(result, vec![Path::new(FILE_NAME)]);

        let buf = std::fs::read(new_dir_path.join(&result[0]))?;

        assert_eq!(&buf, FILE_CONTENTS);
        Ok(())
    }

    #[test]
    fn test_execute_bad_commands() {
        // This fails even before executing the command because it doesn't exist.
        let result = execute_command(Command::new("/this/does/not/exist"));
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.to_string().contains("Unable to execute"));

        // This fails due to a bad status code of the command.
        let result = execute_command(Command::new("false"));
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.to_string().contains("Got bad status code"));
    }

    #[test]
    fn test_execute_good_command() {
        let result = execute_command(Command::new("ls"));
        assert!(result.is_ok());
    }

    #[test]
    fn test_insert_partition() {
        const STATE_START_LBA: u64 = 2;
        const STATE_END_LBA: u64 = 5;
        const NEW_PART_SIZE_BLOCKS: u64 = 3;

        let mock_stateful_info = GptPartitionEntry {
            starting_lba: LbaLe::from_u64(STATE_START_LBA),
            ending_lba: LbaLe::from_u64(STATE_END_LBA),
            name: "STATE".parse().unwrap(),
            ..Default::default()
        };

        let new_state_range = shrink_partition_by(mock_stateful_info, NEW_PART_SIZE_BLOCKS);
        assert!(new_state_range.is_ok());
        let new_state_range = new_state_range.unwrap();
        assert_eq!(new_state_range.start().to_u64(), STATE_START_LBA);
        assert_eq!(
            new_state_range.end().to_u64(),
            STATE_END_LBA - NEW_PART_SIZE_BLOCKS
        );

        let new_part_range = add_partition_after(new_state_range, NEW_PART_SIZE_BLOCKS);
        assert!(new_part_range.is_ok());
        let new_part_range = new_part_range.unwrap();
        assert_eq!(
            new_part_range.start().to_u64(),
            STATE_END_LBA - NEW_PART_SIZE_BLOCKS + 1
        );
        assert_eq!(new_part_range.end().to_u64(), STATE_END_LBA);
    }

    #[test]
    fn test_insert_partition_fails() {
        const STATE_START: u64 = 0;
        // Assuming a block size of 512.
        const STATE_END: u64 = 10_000_000_000 / 512;
        const NEW_PART_SIZE: u64 = 11_000_000_000 / 512;

        let mock_stateful_info = GptPartitionEntry {
            starting_lba: LbaLe::from_u64(STATE_START),
            ending_lba: LbaLe::from_u64(STATE_END),
            name: "STATE".parse().unwrap(),
            ..Default::default()
        };

        let new_state_range = shrink_partition_by(mock_stateful_info, NEW_PART_SIZE);
        assert!(new_state_range.is_err());
    }
}
