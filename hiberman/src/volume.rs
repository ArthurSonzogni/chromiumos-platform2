// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement volume and disk management.

use anyhow::{Context, Result};
use libc::{self, c_ulong, c_void};
use log::{debug, info, warn};

use std::ffi::{CString, OsStr};
use std::fs::{read_link, OpenOptions};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use crate::files::HIBERNATE_DIR;
use crate::hiberutil::{
    checked_command, checked_command_output, get_device_mounted_at_dir, get_page_size,
    get_total_memory_pages, log_io_duration, stateful_block_partition_one, HibernateError,
};
use crate::lvm::{create_thin_volume, get_vg_name, lv_path, thicken_thin_volume};

/// Define the name of the hibernate logical volume.
const HIBER_VOLUME_NAME: &str = "hibervol";
/// Define the size to include in the hibernate volume for accumulating all the
/// writes in the resume boot. Usually (as of mid 2022) this is about 32MB. Size
/// it way up to be safe.
const MAX_SNAPSHOT_BYTES: i64 = 1024 * 1024 * 1024;

/// Define the amount of extra space in the hibernate volume to account for
/// things like file system overhead and general safety margin.
const HIBER_VOLUME_FUDGE_BYTES: i64 = 1024 * 1024 * 1024;

/// Define the number of sectors per dm-snapshot chunk.
const DM_SNAPSHOT_CHUNK_SIZE: usize = 8;

pub struct VolumeManager {
    vg_name: String,
    hibervol: Option<ActiveMount>,
}

impl VolumeManager {
    /// Create a new VolumeManager.
    pub fn new() -> Result<Self> {
        let partition1 = stateful_block_partition_one()?;
        let vg_name = get_vg_name(&partition1)?;
        Ok(Self {
            vg_name,
            hibervol: None,
        })
    }

    /// Set up the hibernate logical volume.
    pub fn setup_hibernate_lv(&mut self, create: bool) -> Result<()> {
        let path = lv_path(&self.vg_name, HIBER_VOLUME_NAME);
        if !path.exists() {
            if create {
                self.create_hibernate_lv()?;
            } else {
                return Err(HibernateError::HibernateVolumeError())
                    .context("Missing hibernate volume");
            }
        }

        // Mount the LV to the hibernate directory unless it's already mounted.
        if get_device_mounted_at_dir(HIBERNATE_DIR).is_err() {
            let hibernate_dir = Path::new(HIBERNATE_DIR);
            self.hibervol = Some(ActiveMount::new(
                path.as_path(),
                hibernate_dir,
                "ext4",
                0,
                "",
                false,
            )?);
        }

        Ok(())
    }

    /// Create the hibernate volume.
    fn create_hibernate_lv(&mut self) -> Result<()> {
        info!("Creating hibernate logical volume");
        let path = lv_path(&self.vg_name, HIBER_VOLUME_NAME);
        let size_mb = Self::hiber_volume_mb();
        let start = Instant::now();
        create_thin_volume(&self.vg_name, size_mb, HIBER_VOLUME_NAME)
            .context("Failed to create thin volume")?;
        thicken_thin_volume(&path, size_mb).context("Failed to thicken volume")?;
        // Use -K to tell mkfs not to run a discard on the block device, which
        // would destroy all the nice thickening done above.
        checked_command_output(Command::new("/sbin/mkfs.ext4").arg("-K").arg(path))
            .context("Cannot format hibernate volume")?;
        log_io_duration(
            "Created hibernate logical volume",
            size_mb * 1024 * 1024,
            start.elapsed(),
        );
        Ok(())
    }

    /// Figure out the appropriate size for the hibernate volume.
    fn hiber_volume_mb() -> i64 {
        let total_mem = (get_total_memory_pages() as i64) * (get_page_size() as i64);
        let size_bytes = total_mem + MAX_SNAPSHOT_BYTES + HIBER_VOLUME_FUDGE_BYTES;
        size_bytes / (1024 * 1024)
    }

    /// Set up dm-snapshots for the stateful LVs.
    pub fn setup_stateful_snapshots(&mut self) -> Result<()> {
        self.setup_snapshot("unencrypted", 1024)?;
        self.setup_snapshot("dev-image", 256)?;
        self.setup_snapshot("encstateful", 512)
    }

    /// Set up a dm-snapshot for a single named LV.
    fn setup_snapshot(&mut self, name: &str, size_mb: u64) -> Result<()> {
        let path = Path::new(HIBERNATE_DIR).join(format!("{}-snapshot", name));
        let file = OpenOptions::new()
            .write(true)
            .create(true)
            .open(&path)
            .context(format!("Failed to open snapshot file: {}", path.display()))?;

        file.set_len(size_mb * 1024 * 1024)?;
        drop(file);
        let loop_path = Self::setup_loop_device(&path)?;
        let origin_lv = read_link(lv_path(&self.vg_name, name))?;
        Self::setup_dm_snapshot(origin_lv, loop_path, name, DM_SNAPSHOT_CHUNK_SIZE)
            .context(format!("Failed to setup dm-snapshot for {}", name))
    }

    /// Set up a loop device for the given file and return the path to it.
    fn setup_loop_device<P: AsRef<OsStr>>(file_path: P) -> Result<PathBuf> {
        let output = checked_command_output(Command::new("/sbin/losetup").args([
            "--show",
            "-f",
            &file_path.as_ref().to_string_lossy(),
        ]))
        .context("Cannot create loop device")?;

        Ok(PathBuf::from(
            String::from_utf8_lossy(&output.stdout).trim(),
        ))
    }

    /// Set up a dm-snapshot origin and snapshot. Example:
    ///   dmsetup create stateful-origin --table \
    ///     "0 ${STATE_SIZE} snapshot-origin ${STATE_DEV}"
    ///   dmsetup create stateful-rw --table \
    ///     "0 ${STATE_SIZE} snapshot ${STATE_DEV} ${COW_LOOP} P 8"
    fn setup_dm_snapshot<P: AsRef<OsStr>>(
        origin: P,
        snapshot: P,
        name: &str,
        chunk_size: usize,
    ) -> Result<()> {
        let size_sectors = Self::get_blockdev_size(&origin)?;
        let origin_string = origin.as_ref().to_string_lossy();
        let snapshot_string = snapshot.as_ref().to_string_lossy();
        let origin_table = format!("0 {} snapshot-origin {}", size_sectors, &origin_string);
        debug!("Creating snapshot origin: {}", &origin_table);
        checked_command(Command::new("/sbin/dmsetup").args([
            "create",
            &format!("{}-origin", name),
            "--table",
            &origin_table,
        ]))
        .context(format!("Cannot setup snapshot-origin for {}", name))?;

        let snapshot_table = format!(
            "0 {} snapshot {} {} P {}",
            size_sectors, &origin_string, &snapshot_string, chunk_size
        );
        debug!("Creating snapshot table: {}", &snapshot_table);
        checked_command(Command::new("/sbin/dmsetup").args([
            "create",
            &format!("{}-rw", name),
            "--table",
            &snapshot_table,
        ]))
        .context(format!("Cannot setup dm-snapshot for {}", name))?;

        Ok(())
    }

    /// Get the size of the block device at the given path, in sectors.
    /// Note: We could save the external process by opening the device and
    /// using the BLKGETSIZE64 ioctl instead.
    fn get_blockdev_size<P: AsRef<OsStr>>(path: P) -> Result<i64> {
        let output =
            checked_command_output(Command::new("/sbin/blockdev").arg("--getsz").arg(path))
                .context("Failed to get block device size")?;
        let size_str = String::from_utf8_lossy(&output.stdout);
        size_str
            .trim()
            .parse()
            .context("Failed to parse blockdev size")
    }
}

struct ActiveMount {
    path: Option<CString>,
}

impl ActiveMount {
    /// Mount a new file system somewhere.
    pub fn new<P: AsRef<OsStr>>(
        device: P,
        path: P,
        fs_type: &str,
        flags: c_ulong,
        data: &str,
        unmount_on_drop: bool,
    ) -> Result<Self> {
        let device_string = CString::new(device.as_ref().as_bytes())?;
        let path_string = CString::new(path.as_ref().as_bytes())?;
        let fs_string = CString::new(fs_type)?;
        let data_string = CString::new(data)?;
        info!(
            "Mounting {} to {}",
            device_string.to_string_lossy(),
            path_string.to_string_lossy()
        );
        // This is safe because mount does not affect memory layout.
        unsafe {
            let rc = libc::mount(
                device_string.as_ptr(),
                path_string.as_ptr(),
                fs_string.as_ptr(),
                flags,
                data_string.as_ptr() as *const c_void,
            );
            if rc < 0 {
                return Err(sys_util::Error::last()).context("Failed to mount hibernate volume");
            }
        }

        let path = if unmount_on_drop {
            Some(path_string)
        } else {
            None
        };

        Ok(Self { path })
    }
}

impl Drop for ActiveMount {
    fn drop(&mut self) {
        let path = self.path.take();
        if let Some(path) = path {
            // Attempt to unmount the file system.
            // This is safe because unmount does not affect memory.
            unsafe {
                info!("Unmounting {}", path.to_string_lossy());
                let rc = libc::umount(path.as_ptr());
                if rc < 0 {
                    warn!(
                        "Failed to unmount {}: {}",
                        path.to_string_lossy(),
                        sys_util::Error::last()
                    );
                }
            }
        }
    }
}
