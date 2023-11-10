// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for interacting with the disk.

use std::{
    os::unix::prelude::OsStrExt,
    path::{Path, PathBuf},
};

/// Get a disk partition device path.
///
/// This handles inserting a 'p' before the number if needed and special cases
/// for /dev/disk/by-id or /dev/disk/by-id.
/// Please note: The path needs to be a child of either:
/// - /dev
/// - /dev/disk/by-path
/// - /dev/disk/by-id
pub fn get_partition_device(disk_device: &Path, num: u32) -> Option<PathBuf> {
    return match disk_device.parent() {
        Some(path) => {
            if path.as_os_str() == "/dev" {
                return Some(get_partition_device_dev(disk_device, num));
            } else if path.as_os_str() == "/dev/disk/by-id"
                || path.as_os_str() == "/dev/disk/by-path"
            {
                return Some(get_partition_device_by_path_or_id(disk_device, num));
            }

            None
        }
        None => None,
    };
}

fn get_partition_device_dev(disk_device: &Path, num: u32) -> PathBuf {
    let mut buf = disk_device.as_os_str().to_os_string();

    // If the disk path ends in a number, e.g. "/dev/nvme0n1", append
    // a "p" before the partition number.
    if let Some(byte) = buf.as_bytes().last() {
        if byte.is_ascii_digit() {
            buf.push("p");
        }
    }

    buf.push(num.to_string());

    PathBuf::from(buf)
}

fn get_partition_device_by_path_or_id(disk_device: &Path, num: u32) -> PathBuf {
    let mut buf = disk_device.as_os_str().to_os_string();
    // Both by-path and by-id have a -partX postfix.
    // E.g. partition 4 of `/dev/disk/by-id/nvme-eui.e8238fa6bf530001001b448b4566aa1a` is:
    // `/dev/disk/by-id/nvme-eui.e8238fa6bf530001001b448b4566aa1a-part4`
    buf.push(format!("-part{}", num));

    PathBuf::from(buf)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_partition_device() {
        // Testing /dev variants.
        let result = get_partition_device(Path::new("/dev/nvme0n1"), 4);
        assert!(result.is_some());
        assert_eq!(Path::new("/dev/nvme0n1p4").to_path_buf(), result.unwrap());

        let result = get_partition_device(Path::new("/dev/loop0"), 1);
        assert!(result.is_some());
        assert_eq!(Path::new("/dev/loop0p1").to_path_buf(), result.unwrap());

        let result = get_partition_device(Path::new("/dev/mmcblk0"), 2);
        assert!(result.is_some());
        assert_eq!(Path::new("/dev/mmcblk0p2").to_path_buf(), result.unwrap());

        let result = get_partition_device(Path::new("/dev/sda"), 3);
        assert!(result.is_some());
        assert_eq!(Path::new("/dev/sda3").to_path_buf(), result.unwrap());

        // Testing /dev/disk/by-id variants.
        let result = get_partition_device(
            Path::new("/dev/disk/by-id/nvme-eui.e8238fa6bf530001001b448b4566aa1a"),
            3,
        );
        assert!(result.is_some());
        assert_eq!(
            Path::new("/dev/disk/by-id/nvme-eui.e8238fa6bf530001001b448b4566aa1a-part3")
                .to_path_buf(),
            result.unwrap()
        );

        let result = get_partition_device(Path::new("/dev/disk/by-id/scsi-0PersistentDisk"), 2);
        assert!(result.is_some());
        assert_eq!(
            Path::new("/dev/disk/by-id/scsi-0PersistentDisk-part2").to_path_buf(),
            result.unwrap()
        );

        // Testing /dev/disk/by-path variants.
        let result = get_partition_device(Path::new("/dev/disk/by-path/pci-0000:00:1f.2-ata-1"), 1);
        assert!(result.is_some());
        assert_eq!(
            Path::new("/dev/disk/by-path/pci-0000:00:1f.2-ata-1-part1").to_path_buf(),
            result.unwrap()
        );

        let result =
            get_partition_device(Path::new("/dev/disk/by-path/pci-0000:04:00.0-nvme-1"), 4);
        assert!(result.is_some());
        assert_eq!(
            Path::new("/dev/disk/by-path/pci-0000:04:00.0-nvme-1-part4").to_path_buf(),
            result.unwrap()
        );
    }
}
