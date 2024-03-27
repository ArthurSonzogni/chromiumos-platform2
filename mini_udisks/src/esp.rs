// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, bail, Context, Result};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};

/// Information about the ESP partition.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Esp {
    /// Partition number in the GPT, e.g. 12 if the device path is
    /// "/dev/sda12".
    pub partition_num: u32,

    /// Full path of the ESP partition device, e.g. "/dev/sda12".
    ///
    /// Store as a string rather than a path, since that's what the
    /// D-Bus API uses.
    pub device_path: String,

    /// File name of the ESP partition device, e.g. "sda12".
    pub device_name: String,

    /// Unique GUID of the partition (note -- not the same thing as the
    /// partition type GUID).
    pub unique_id: String,
}

impl Esp {
    /// Mount point for the ESP.
    ///
    /// The ESP is mounted here by the code in
    /// platform2/init/startup/uefi_startup.cc.
    pub const MOUNT_POINT: &'static str = "/efi";

    /// Partition type GUID that identifies the ESP.
    ///
    /// Defined in:
    /// https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
    pub const TYPE_GUID: &'static str = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

    /// Find the ESP mount, and get information about the corresponding
    /// partition.
    ///
    /// If the ESP is not mounted (as is the case on non-UEFI systems),
    /// returns `Ok(None)`.
    pub fn find() -> Result<Option<Self>> {
        Self::find_with_root(&Root::new())
    }

    fn find_with_root(root: &Root) -> Result<Option<Self>> {
        let esp_device = get_esp_device_path(root)?;
        if let Some(esp_device) = esp_device {
            let device_path = esp_device
                .to_str()
                .context("esp device path is not utf-8")?
                .to_string();
            let device_name = esp_device
                .file_name()
                .context("missing esp file name")?
                .to_str()
                .context("esp device name is not utf-8")?
                .to_string();
            let partition_num = get_partition_num_from_device_path(&device_path)?;
            let unique_id = get_partuuid_for_device(&esp_device, root)?;
            Ok(Some(Self {
                partition_num,
                device_path,
                device_name,
                unique_id,
            }))
        } else {
            Ok(None)
        }
    }
}

/// Get the partition number from a partition device path.
///
/// For example, given "/dev/sda12" or "/dev/nvme0n1p12", this will
/// return 12.
fn get_partition_num_from_device_path(partition_path: &str) -> Result<u32> {
    if !partition_path.is_ascii() {
        bail!("partition device path is not ascii: {partition_path}");
    }
    let index = partition_path
        .rfind(|c: char| !c.is_ascii_digit())
        .context("invalid partition device path")?;
    let num = &partition_path[index + 1..];
    num.parse().context("invalid partition number")
}

/// Path of the filesystem root. Normally `/`, but can be changed for
/// testing.
struct Root(PathBuf);

impl Root {
    fn new() -> Self {
        Self(PathBuf::from("/"))
    }

    fn partuuid_dir(&self) -> PathBuf {
        self.0.join("dev/disk/by-partuuid")
    }

    fn mountinfo_path(&self) -> PathBuf {
        self.0.join("proc/self/mountinfo")
    }
}

/// Get the partition device path of the ESP.
///
/// This looks for the `/efi` mount to get the target. If `/efi` is not
/// mounted, returns `None`.
fn get_esp_device_path(root: &Root) -> Result<Option<PathBuf>> {
    let mountinfo_path = root.mountinfo_path();

    let file = File::open(&mountinfo_path)
        .context(format!("failed to open {}", mountinfo_path.display()))?;
    let mounts = BufReader::new(file);

    // File format documentation:
    // https://www.kernel.org/doc/html/latest/filesystems/proc.html#proc-pid-mountinfo-information-about-mounts
    for line in mounts.lines() {
        let line = line.context(format!(
            "failed to read line in {}",
            mountinfo_path.display()
        ))?;
        let parts: Vec<_> = line.split_whitespace().collect();

        let Some(mount_point) = parts.get(4) else {
            continue;
        };

        let Some(separator_index) = parts.iter().position(|part| *part == "-") else {
            continue;
        };

        let Some(device) = parts.get(separator_index + 2) else {
            continue;
        };

        if *mount_point == Esp::MOUNT_POINT {
            return Ok(Some(PathBuf::from(device)));
        }
    }

    Ok(None)
}

/// Get the unique ID of a partition.
fn get_partuuid_for_device(device: &Path, root: &Root) -> Result<String> {
    let dir = root.partuuid_dir();
    for entry in dir.read_dir().context("failed to read by-partuuid dir")? {
        let entry = entry.context("failed to read by-partuuid entry")?;

        // Get the symlink target, e.g. "../../sda12".
        let link_target = entry
            .path()
            .read_link()
            .context("failed to read by-partuuid symlink")?;

        // Get the absolute symlink target,
        // e.g. "/dev/disk/by-partuuid/../../sda12".
        let link_target = dir.join(&link_target);

        // Canonicalize the symlink target, e.g. "/dev/sda12".
        let canonical = link_target
            .canonicalize()
            .context("failed to canonicalize by-partuuid symlink target")?;

        if canonical == device {
            return entry
                .file_name()
                .into_string()
                .map_err(|_| anyhow!("invalid partuuid"));
        }
    }

    // No matching partition UUID.
    bail!("no partuuid found for {}", device.display());
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::unix::fs::symlink;

    #[test]
    fn test_root() {
        assert_eq!(Root::new().0, Path::new("/"));
    }

    #[test]
    fn test_get_partition_num_from_device_path() {
        assert_eq!(get_partition_num_from_device_path("/dev/sda1").unwrap(), 1);
        assert_eq!(
            get_partition_num_from_device_path("/dev/sda12").unwrap(),
            12
        );

        assert_eq!(
            get_partition_num_from_device_path("/dev/nvme0n1p1").unwrap(),
            1
        );
        assert_eq!(
            get_partition_num_from_device_path("/dev/nvme0n1p12").unwrap(),
            12
        );

        assert!(get_partition_num_from_device_path("").is_err());
        assert!(get_partition_num_from_device_path("/dev/sda").is_err());
        assert!(get_partition_num_from_device_path("/dev/sda😎").is_err());
    }

    #[test]
    fn test_get_esp_device_path() {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir_all(dir.path().join("proc/self")).unwrap();
        let root = Root(dir.path().to_owned());

        // Error if the file doesn't exist.
        assert!(get_esp_device_path(&root).is_err());

        // If the ESP is not mounted, return None rather than an error.
        fs::write(root.mountinfo_path(), "").unwrap();
        assert_eq!(get_esp_device_path(&root).unwrap(), None);

        // Ignore mount lines with too-few fields.
        let lines = "\n1 2 3 4 5\n1 2 3 4 5 - 1";
        fs::write(root.mountinfo_path(), lines).unwrap();
        assert_eq!(get_esp_device_path(&root).unwrap(), None);

        // Test with a copy of an actual mountinfo file.
        fs::write(root.mountinfo_path(), include_bytes!("test_mountinfo.txt")).unwrap();
        assert_eq!(
            get_esp_device_path(&root).unwrap(),
            Some(PathBuf::from("/dev/sda12"))
        );

        // Test a mount point with spaces in it, produced with:
        // mount /dev/sda12 '/tmp/efi space'
        let line = r"871 26 8:12 / /tmp/efi\040space rw,relatime - vfat /dev/sda12";
        fs::write(root.mountinfo_path(), line).unwrap();
        assert_eq!(get_esp_device_path(&root).unwrap(), None,);
    }

    #[test]
    fn test_get_partuuid_for_device() {
        let dir = tempfile::tempdir().unwrap();
        let root = Root(dir.path().to_owned());

        let esp_device = root.0.join("dev/sda12");

        // Error: directory doesn't exist.
        assert!(get_partuuid_for_device(&esp_device, &root).is_err());

        // Create the by-partuuid directory.
        fs::create_dir_all(root.partuuid_dir()).unwrap();

        // Error: dev/sda12 doesn't exist, so canonicalizeation will fail.
        let uuid = "ac45b957-1eab-6c48-98de-71b63a017b6b";
        symlink("../../sda12", root.partuuid_dir().join(uuid)).unwrap();
        assert!(get_partuuid_for_device(&esp_device, &root).is_err());

        // Success.
        fs::write(root.0.join("dev/sda12"), "").unwrap();
        assert_eq!(get_partuuid_for_device(&esp_device, &root).unwrap(), uuid);

        // Error: no matching device found.
        assert!(get_partuuid_for_device(&root.0.join("dev/sda1"), &root).is_err());
    }

    #[test]
    fn test_esp_find() {
        let dir = tempfile::tempdir().unwrap();
        let root = Root(dir.path().to_owned());
        let device_path = root.0.join("dev/sda12").to_str().unwrap().to_string();

        fs::create_dir_all(dir.path().join("proc/self")).unwrap();
        fs::write(root.mountinfo_path(), "").unwrap();

        assert!(Esp::find_with_root(&root).unwrap().is_none());

        fs::write(
            root.mountinfo_path(),
            format!("49 20 8:12 / /efi opts - vfat {device_path}"),
        )
        .unwrap();
        assert!(Esp::find_with_root(&root).is_err());

        fs::create_dir_all(root.partuuid_dir()).unwrap();
        let uuid = "ac45b957-1eab-6c48-98de-71b63a017b6b";
        symlink("../../sda12", root.partuuid_dir().join(uuid)).unwrap();
        fs::write(&device_path, "").unwrap();
        assert_eq!(
            Esp::find_with_root(&root).unwrap().unwrap(),
            Esp {
                partition_num: 12,
                device_path,
                device_name: "sda12".to_string(),
                unique_id: uuid.to_string(),
            }
        );
    }
}
