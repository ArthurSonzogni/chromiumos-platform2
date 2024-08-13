// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{error, info};
use nix::mount::MsFlags;
use std::path::{Path, PathBuf};
use std::{fmt, io};
use tempfile::TempDir;
use thiserror::Error;

/// A filesystem type for passing to `Mount::new`.
pub enum FsType {
    Ext4,
    Vfat,
}

impl fmt::Display for FsType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Ext4 => f.write_str("ext4"),
            Self::Vfat => f.write_str("vfat"),
        }
    }
}

/// Common error type for `Mount` failures.
#[derive(Error, Debug)]
pub enum MountError {
    #[error("failed to create a temp dir to mount to")]
    Tempdir(#[source] io::Error),
    #[error("failed to mount {0}")]
    Mount(PathBuf, #[source] io::Error),
    #[error("failed to umount {0}")]
    Unmount(PathBuf, #[source] io::Error),
    #[error("failed to remove the temp dir")]
    RemoveTempdir(#[source] io::Error),
}

/// Mounts a file system on a temporary directory, then unmounts when dropped.
pub struct Mount {
    // The temporary directory we're mounted to.
    // Set to `None` when unmounting.
    mount_point: Option<TempDir>,
}

// Wrapper around `nix::mount::umount` used by Mount.
fn unmount_impl(path: &Path) -> Result<(), MountError> {
    info!("Unmounting {}", path.display());
    nix::mount::umount(path).map_err(|e| MountError::Unmount(path.to_path_buf(), e.into()))
}

impl Mount {
    /// Create a tempdir and mount `source` to it.
    ///
    /// Currently this will always mount with the standard security flags nodev,
    /// noexec, and nosuid.
    pub fn new(source: &Path, fs_type: FsType) -> Result<Self, MountError> {
        let data: Option<&Path> = None;
        let flags = MsFlags::MS_NODEV | MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID;
        let fs_str = fs_type.to_string();
        let mount_point = TempDir::new().map_err(MountError::Tempdir)?;
        let mount_point = Some(mount_point);
        let mount_holder = Self { mount_point };

        info!(
            "mounting {} at {} with type {} and flags {:?}",
            source.display(),
            mount_holder.mount_path().display(),
            fs_type,
            flags
        );

        nix::mount::mount(
            Some(source),
            mount_holder.mount_path(),
            Some(fs_str.as_str()),
            flags,
            data,
        )
        .map_err(|e| MountError::Mount(source.to_path_buf(), e.into()))?;

        Ok(mount_holder)
    }

    /// The Path to the mounted files.
    pub fn mount_path(&self) -> &Path {
        // OK to unwrap: We only have None if we've unmounted, which consumes
        // self.
        self.mount_point.as_ref().unwrap().path()
    }

    // TODO(b/362703366): consider adding ability to retry on failure.
    /// Explicily unmounts and removes the tempdir. Consumes self.
    pub fn unmount(mut self) -> Result<(), MountError> {
        // Take the `TempDir`, leaving `self.mount_point` as `None`, so that we
        // can consume it. We must `take` before calling `unmount_impl` or an
        // `Err` from `unmount_impl` leaves `Some` in `mount_point` and when
        // `drop` is eventually called it will `unmount_impl` a second time.
        // OK to unwrap: we only have `None` if we've called `unmount` before,
        // but it consumes self so that can't have happened.
        let mount_point = self.mount_point.take().unwrap();

        unmount_impl(mount_point.path())?;

        // We explicitly close the `TempDir` to return any errors.
        mount_point.close().map_err(MountError::RemoveTempdir)
    }
}

impl Drop for Mount {
    fn drop(&mut self) {
        // If `unmount` has been called, `mount_point` will be `None`, and
        // we'll already have unmounted and removed the temp dir.
        if self.mount_point.is_none() {
            return;
        }

        if let Err(err) = unmount_impl(self.mount_path()) {
            // No way to propagate the error from drop(), so just
            // print it.
            error!(
                "Error unmounting temp directory at {}: {}",
                self.mount_path().display(),
                err
            );
        }
    }
}
