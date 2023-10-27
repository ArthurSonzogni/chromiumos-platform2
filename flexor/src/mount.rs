// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::path::Path;

use anyhow::Result;
use log::{error, info};
use nix::mount::{mount, umount, MsFlags};
use tempfile::TempDir;

pub enum FsType {
    EXT4,
    FAT32,
}

impl fmt::Display for FsType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EXT4 => f.write_str("ext4"),
            Self::FAT32 => f.write_str("fat32"),
        }
    }
}

// Mounts a disk to a tempdir and unmounts it on destruction.
pub struct Mount {
    tempdir: TempDir,
}

impl Mount {
    pub fn mount_by_path<P: AsRef<Path>>(path: P, fs_type: FsType) -> Result<Self> {
        let tempdir = TempDir::new()?;
        let flags = MsFlags::MS_NODEV | MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID;
        let fs_str = fs_type.to_string();
        let data: Option<&Path> = None;

        info!(
            "Mounting {} to {}",
            path.as_ref().display(),
            tempdir.path().display()
        );
        mount(
            Some(path.as_ref()),
            tempdir.path(),
            Some(Path::new(&fs_str)),
            flags,
            data,
        )?;

        Ok(Self { tempdir })
    }

    pub fn mount_path(&self) -> &Path {
        self.tempdir.path()
    }

    fn umount(&self) -> Result<()> {
        info!("Unmounting {}", self.tempdir.path().display());
        umount(self.tempdir.path())?;

        Ok(())
    }
}

impl Drop for Mount {
    fn drop(&mut self) {
        if let Err(err) = self.umount() {
            error!(
                "Error unmounting temp directory at {}: {}",
                self.tempdir.path().display(),
                err
            );
        }
    }
}
