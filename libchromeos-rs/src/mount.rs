// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{error, info};
pub use nix::mount::MsFlags;
use std::ffi::{OsStr, OsString};
use std::fmt::Write;
use std::path::{Path, PathBuf};
use std::{fmt, io};
use tempfile::TempDir;
use thiserror::Error;

/// A filesystem type for passing to mount calls.
pub enum FsType {
    Efivarfs,
    Ext4,
    Tmpfs,
    Vfat,
}

impl fmt::Display for FsType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Efivarfs => f.write_str("efivarfs"),
            Self::Ext4 => f.write_str("ext4"),
            Self::Tmpfs => f.write_str("tmpfs"),
            Self::Vfat => f.write_str("vfat"),
        }
    }
}

/// Common error type for mount failures.
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

// Wrapper around `nix::mount::umount` used by `Mount`.
fn unmount_impl(path: &Path) -> Result<(), MountError> {
    info!("Unmounting {}", path.display());
    nix::mount::umount(path).map_err(|e| MountError::Unmount(path.to_path_buf(), e.into()))
}

/// Represents the different ways we have of managing our mount points.
enum MountPoint {
    /// Mounts a file system on an existing directory, then unmounts when dropped.
    ExistingDir(PathBuf),
    /// Mounts a file system on a temporary directory, then unmounts when dropped..
    TempBacked(TempDir),
}

/// A managed mount.
///
/// `mount_point` is set to `None` when unmounting. Use the Builder to construct.
pub struct Mount {
    mount_point: Option<MountPoint>,
}

impl Mount {
    /// The Path to the mounted files.
    pub fn mount_path(&self) -> &Path {
        // OK to unwrap: We only have None if we've unmounted, which consumes self.
        let mount_point = self.mount_point.as_ref().unwrap();
        match mount_point {
            MountPoint::ExistingDir(dir) => dir,
            MountPoint::TempBacked(tempdir) => tempdir.path(),
        }
    }

    // TODO(b/362703366): consider adding ability to retry on failure.
    /// Explicily unmounts and does any other necessary cleanup. Consumes self.
    pub fn unmount(mut self) -> Result<(), MountError> {
        // Take the `MountPoint`, leaving `mount_point` as `None` so that we can consume it. We must
        // `take` before calling `unmount_impl` or an `Err` from `unmount_impl` leaves `Some` in
        // `mount_point` and when `drop` is eventually called it will `unmount_impl` a second time.
        // OK to unwrap: we only have `None` if we've called `unmount` before, but it consumes self
        // so that can't have happened.
        let mount_point = self.mount_point.take().unwrap();

        match mount_point {
            MountPoint::ExistingDir(dir) => unmount_impl(&dir),
            MountPoint::TempBacked(tempdir) => {
                unmount_impl(tempdir.path())?;

                // We explicitly close the `TempDir` to return any errors.
                tempdir.close().map_err(MountError::RemoveTempdir)
            }
        }
    }
}

impl Drop for Mount {
    fn drop(&mut self) {
        // If `unmount` has been called the Option will be `None`, and
        // we'll already have unmounted and done any other cleanup.
        if self.mount_point.is_none() {
            return;
        }

        if let Err(err) = unmount_impl(self.mount_path()) {
            // No way to propagate the error from drop(), so just
            // print it.
            error!("Error unmounting {}: {}", self.mount_path().display(), err);
        }

        // In the `MountPoint::TempBacked` case the tempdir will clean itself up here.
    }
}

/// A mount Builder, for creating a mount with specific configuration.
pub struct Builder {
    /// The file to mount from.
    source: PathBuf,
    /// The filesystem type.
    fs_type: Option<FsType>,
    /// Defaults to the secure set of {nodev, noexec, nosuid}.
    flags: MsFlags,
    /// Filesystem-specific data; see `man 2 mount` for more info.
    data: Option<OsString>,
}

impl Builder {
    /// Create the base for mounting, with default flags and the specified `source`.
    pub fn new(source: &Path) -> Self {
        Self {
            source: source.to_path_buf(),
            fs_type: None,
            flags: MsFlags::MS_NODEV | MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID,
            data: None,
        }
    }

    /// Set the file system type.
    pub fn fs_type(mut self, fs_type: FsType) -> Self {
        self.fs_type = Some(fs_type);

        self
    }

    /// Add flags.
    ///
    /// By default the standard security flags nodev, noexec, and nosuid are
    /// set. To unset them use `remove_flags`.
    pub fn add_flags(mut self, flags: MsFlags) -> Self {
        self.flags.insert(flags);

        self
    }

    /// Remove flags.
    ///
    /// This allows for the removal of the default flags.
    pub fn remove_flags(mut self, flags: MsFlags) -> Self {
        self.flags.remove(flags);

        self
    }

    /// Set the filesystem-specific data.
    ///
    /// See mount(8) for options for each filesystem.
    pub fn data(mut self, data: &OsStr) -> Self {
        self.data = Some(data.to_owned());

        self
    }

    // The bit that does the actual mounting.
    fn mount_impl(&self, target: &Path) -> Result<(), MountError> {
        let fs_str = self.fs_type.as_ref().map(|t| t.to_string());

        let mut message = format!(
            "mounting {} at {} with",
            self.source.display(),
            target.display()
        );
        if let Some(fs_str) = &fs_str {
            write!(&mut message, " type: '{fs_str}'").unwrap();
        }
        write!(&mut message, " flags: '{:?}'", self.flags).unwrap();
        if let Some(data) = &self.data {
            write!(&mut message, " data: '{:?}'", data).unwrap();
        }
        info!("{}", message);

        nix::mount::mount(
            Some(self.source.as_path()),
            target,
            fs_str.as_deref(),
            self.flags,
            self.data.as_deref(),
        )
        .map_err(|e| MountError::Mount(self.source.to_path_buf(), e.into()))?;

        Ok(())
    }

    /// Mount the source to an owned temporary dir and return a `TempBackedMount` or an error.
    ///
    /// Consumes self (the Builder).
    pub fn temp_backed_mount(self) -> Result<Mount, MountError> {
        let mount_point = TempDir::new().map_err(MountError::Tempdir)?;
        let mount_point = Some(MountPoint::TempBacked(mount_point));
        let mount = Mount { mount_point };

        self.mount_impl(mount.mount_path())?;

        Ok(mount)
    }

    /// Mount the source to the `target` and return a `Mount` or an error.
    pub fn mount(self, target: &Path) -> Result<Mount, MountError> {
        let mount_point = Some(MountPoint::ExistingDir(target.to_path_buf()));
        let mount = Mount { mount_point };

        self.mount_impl(target)?;

        Ok(mount)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::fs::read_to_string;
    use std::process::Command;

    // Find info about our mount from /proc/mounts, looking it up by `mount_location`.
    fn get_mount_info(mount_location: &Path) -> Option<String> {
        let mount_path = mount_location.to_str().unwrap();
        let mount_info = read_to_string("/proc/mounts").unwrap();
        mount_info
            .lines()
            .find(|x| x.contains(mount_path))
            .map(str::to_string)
    }

    // These tests require sudo, and won't run as part of the normal in-chroot testing when
    // `emerge`ing, so just leave them as manual tests. You can run these like:
    //     CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_RUNNER='sudo -E' cargo test -- --ignored

    #[test]
    #[ignore]
    fn test_tmpmnt_basic() {
        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            .temp_backed_mount()
            .unwrap();

        // Confirm that the mount is good by touching a file on it.
        let file = mnt.mount_path().join("touched");
        let status = Command::new("touch").arg(file).status().unwrap();

        assert!(status.success());
    }

    #[test]
    #[ignore]
    fn test_tmpmnt_drop() {
        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            .temp_backed_mount()
            .unwrap();

        let path = mnt.mount_path().to_path_buf();

        drop(mnt);

        // The tempfile can't be deleted if the mount is still there,
        // so this is all we need to check.
        assert!(!path.exists());
    }

    #[test]
    #[ignore]
    fn test_mnt_basic() {
        let mount_point = TempDir::new().map_err(MountError::Tempdir).unwrap();

        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            .mount(mount_point.path())
            .unwrap();

        // Confirm that the mount is good by touching a file on it.
        let mut file = mnt.mount_path().to_path_buf();
        file.push("touched");

        let status = Command::new("touch")
            .arg(file.into_os_string())
            .status()
            .unwrap();

        assert!(status.success());
    }

    #[test]
    #[ignore]
    fn test_mnt_drop() {
        let mount_point = TempDir::new().map_err(MountError::Tempdir).unwrap();

        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            .mount(mount_point.path())
            .unwrap();

        drop(mnt);

        // Make sure we're not still mounted.
        let mount_info = get_mount_info(mount_point.path());
        assert!(mount_info.is_none());

        // Make sure our mount point didn't get deleted.
        assert!(mount_point.path().exists());
    }

    #[test]
    #[ignore]
    fn test_builder_flags() {
        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            // Nice unique string to look for.
            .add_flags(MsFlags::MS_DIRSYNC)
            // This is added by default.
            .remove_flags(MsFlags::MS_NODEV)
            .temp_backed_mount()
            .unwrap();

        let mount_info = get_mount_info(mnt.mount_path()).unwrap();

        println!("mount info: {:?}", mount_info);

        assert!(mount_info.contains("dirsync"));
        assert!(!mount_info.contains("nodev"));
    }

    #[test]
    #[ignore]
    fn test_builder_data() {
        let mnt = Builder::new(Path::new("tmpfs"))
            .fs_type(FsType::Tmpfs)
            .data(OsStr::new("size=8k"))
            .temp_backed_mount()
            .unwrap();

        let mount_info = get_mount_info(mnt.mount_path()).unwrap();

        println!("mount info: {:?}", mount_info);

        assert!(mount_info.contains("size=8k"));
    }
}
