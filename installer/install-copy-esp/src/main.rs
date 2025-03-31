// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use clap::Parser;
use fs_err as fs;
use libchromeos::mount;
use libinstall::process_util;
use nix::mount::MsFlags;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Parser, Debug)]
#[command(version, about)]
struct Args {
    /// The source device, e.g. /dev/sda12.
    #[arg(long)]
    src: PathBuf,
    /// The destination device, e.g. /dev/sdb12.
    #[arg(long)]
    dst: PathBuf,
}

/// Get the partition label from a device.
fn get_partition_label(dev_path: &Path) -> Result<String> {
    let mut cmd = Command::new("blkid");
    cmd.args(["-o", "value", "-s", "LABEL"]);
    cmd.arg(dev_path);

    let label = process_util::get_output_as_string(cmd)?;

    Ok(label.trim().to_string())
}

/// Run mkfs.vfat with the appropriate args.
///
/// Tries to match how vfat partitions are made at image creation time, via filesystem_util.sh:
/// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/scripts/build_library/filesystem_util.sh;l=193-196;drc=949a871725585ec5eec3d1db56a2ba2190e1bf00
fn mkfs_vfat(path: &Path, esp_label: &str) -> Result<()> {
    let mut cmd = Command::new("mkfs.vfat");
    // filesystem_util.sh says the -I flag may be needed to ignore a spurious error about formatting
    // a device that already has partitions on it. Hard to verify, so let's just keep it for now.
    cmd.arg("-I");
    cmd.args(["-n", esp_label]);
    cmd.arg(path);

    Ok(process_util::log_and_run_command(cmd)?)
}

/// Standard recursive copy.
///
/// Didn't see one already available in rust_crates.
fn recurse_copy(from: &Path, to: &Path) -> Result<()> {
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let from = entry.path();
        let to = to.join(entry.file_name());
        if from.is_dir() {
            fs::create_dir_all(&to)?;
            recurse_copy(&from, &to)?;
        } else {
            fs::copy(from, to)?;
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    let args = Args::parse();

    let esp_label = get_partition_label(&args.src)?;
    mkfs_vfat(&args.dst, &esp_label)?;

    // A workaround for this flag not being present in nix 0.29.
    // TODO(tbrandston): remove when https://github.com/nix-rust/nix/issues/2626 is fixed.
    // Value pulled from include/uapi/linux/mount.h in the linux kernel.
    let ms_nosymfollow = MsFlags::from_bits_retain(256);

    let src_mount = mount::Builder::new(&args.src)
        .add_flags(MsFlags::MS_RDONLY | ms_nosymfollow)
        .fs_type(mount::FsType::Vfat)
        .temp_backed_mount()?;

    let dst_mount = mount::Builder::new(&args.dst)
        .add_flags(ms_nosymfollow)
        .fs_type(mount::FsType::Vfat)
        .temp_backed_mount()?;

    recurse_copy(src_mount.mount_path(), dst_mount.mount_path())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_recurse_copy() -> Result<()> {
        let src_dir = TempDir::new().unwrap();
        let src_path = src_dir.path();

        // Set up directory tree.
        // root
        //   |-file: A
        //   |-dir:  B/
        //   |  |-dirs: wow/so/nested/
        //   |  |  |-file: deep
        //   |  |-file: C
        //   |-sym: D -> C
        fs::write(src_path.join("A"), "A")?;
        fs::create_dir_all(src_path.join("B/wow/so/nested"))?;
        fs::write(src_path.join("B/wow/so/nested/deep"), "deep")?;
        fs::write(src_path.join("B/C"), "C")?;
        fs::os::unix::fs::symlink(src_path.join("B/C"), src_path.join("D"))?;

        let dst_dir = TempDir::new().unwrap();
        let dst_path = dst_dir.path();

        recurse_copy(src_path, dst_path)?;

        assert_eq!(&std::fs::read_to_string(dst_path.join("A")).unwrap(), "A");
        assert_eq!(
            &std::fs::read_to_string(dst_path.join("B/wow/so/nested/deep")).unwrap(),
            "deep"
        );
        assert_eq!(&std::fs::read_to_string(dst_path.join("B/C")).unwrap(), "C");
        assert_eq!(&std::fs::read_to_string(dst_path.join("D")).unwrap(), "C");

        Ok(())
    }
}
