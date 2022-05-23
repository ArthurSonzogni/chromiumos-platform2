// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement volume and disk management.
// TODO(b/241434344): Things farming out to external processes should be
// implemented in a common helper library instead.

use anyhow::{Context, Result};
use libc::{self, c_ulong, c_void};
use log::{debug, error, info, warn};

use std::ffi::{CString, OsStr};
use std::fs::{read_link, remove_file, OpenOptions};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant};

use crate::files::HIBERNATE_DIR;
use crate::hiberutil::{
    checked_command, checked_command_output, get_device_mounted_at_dir, get_page_size,
    get_total_memory_pages, log_io_duration, reboot_system, stateful_block_partition_one,
    HibernateError,
};
use crate::lvm::{create_thin_volume, get_vg_name, lv_path, thicken_thin_volume};

/// Define the name of the hibernate logical volume.
const HIBER_VOLUME_NAME: &str = "hibervol";

/// Define the known path to the dmsetup utility.
const DMSETUP_PATH: &str = "/sbin/dmsetup";
/// Define the path to the losetup utility.
const LOSETUP_PATH: &str = "/sbin/losetup";

/// Define the size to include in the hibernate volume for accumulating all the
/// writes in the resume boot. Usually (as of mid 2022) this is about 32MB. Size
/// it way up to be safe.
const MAX_SNAPSHOT_BYTES: i64 = 1024 * 1024 * 1024;

/// Define the amount of extra space in the hibernate volume to account for
/// things like file system overhead and general safety margin.
const HIBER_VOLUME_FUDGE_BYTES: i64 = 1024 * 1024 * 1024;

/// Define the number of sectors per dm-snapshot chunk.
const DM_SNAPSHOT_CHUNK_SIZE: usize = 8;

/// The pending stateful merge is simply a token object that when dropped will
/// ask the volume manager to merge the stateful snapshots.
pub struct PendingStatefulMerge<'a> {
    volume_manager: &'a mut VolumeManager,
}

impl<'a> PendingStatefulMerge<'a> {
    pub fn new(volume_manager: &'a mut VolumeManager) -> Self {
        Self { volume_manager }
    }
}

impl Drop for PendingStatefulMerge<'_> {
    fn drop(&mut self) {
        if let Err(e) = self.volume_manager.merge_stateful_snapshots() {
            error!("Failed to merge stateful snapshots: {:?}", e);
            // If we failed to merge the snapshots, the system is in a bad way.
            // Reboot to try and recover.
            reboot_system().unwrap();
        }
    }
}

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
        let path = snapshot_file_path(name);
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
        let output = checked_command_output(Command::new(LOSETUP_PATH).args([
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
        checked_command(Command::new(DMSETUP_PATH).args([
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
        checked_command(Command::new(DMSETUP_PATH).args([
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

    pub fn merge_stateful_snapshots(&mut self) -> Result<()> {
        let mut snapshots = vec![];
        let mut result = Ok(());
        for name in ["unencrypted", "dev-image", "encstateful"] {
            let snapshot = match DmSnapshotMerge::new(name) {
                Ok(o) => match o {
                    Some(s) => s,
                    None => {
                        continue;
                    }
                },
                Err(e) => {
                    error!("Failed to setup snapshot merge for {}: {:?}", name, e);
                    result = Err(e);
                    // Upon failure to start the snapshot merge, try to delete
                    // the snapshot to avoid infinite loops. Since we're already
                    // in a failure path, all we can do upon failure to delete
                    // the snapshot is just log and report the original failure.
                    if let Err(e) = delete_snapshot(name) {
                        error!("Failed to delete snapshot {}: {:?}", name, e);
                    }

                    continue;
                }
            };

            snapshots.push(snapshot);
        }

        wait_for_snapshots_merge(snapshots).and(result)
    }
}

/// Object that tracks the lifetime of a mount, which is potentially unmounted
/// when this object is dropped.
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

/// Object that tracks the lifetime of a temporarily suspended dm-target, and
/// resumes it when the object is dropped.
struct SuspendedDmDevice {
    name: String,
}

impl SuspendedDmDevice {
    pub fn new(name: &str) -> Result<Self> {
        checked_command(Command::new(DMSETUP_PATH).args(["suspend", name]))
            .context(format!("Failed to suspend {}", name))?;

        Ok(Self {
            name: name.to_string(),
        })
    }
}

impl Drop for SuspendedDmDevice {
    fn drop(&mut self) {
        if let Err(e) = checked_command(Command::new(DMSETUP_PATH).args(["resume", &self.name])) {
            error!("Failed to run dmsetup resume {}: {}", &self.name, e);
        }
    }
}

/// Function that waits for a vector of references to DmSnapshotMerge objects to
/// finish. Returns an error if any failed, and waits fully for any that did not
/// fail.
fn wait_for_snapshots_merge(mut snapshots: Vec<DmSnapshotMerge>) -> Result<()> {
    info!("Waiting for {} snapshots to merge", snapshots.len());
    let mut result = Ok(());
    while !snapshots.is_empty() {
        for snapshot in &mut snapshots {
            if let Err(e) = snapshot.check_merge_progress() {
                error!("Failed to check snapshot {}: {:?}", snapshot.name, e);
                result = Err(e);
            }
        }

        // Only keep elements in the list that are still active.
        snapshots.retain(|snapshot| !snapshot.complete && !snapshot.error);

        thread::sleep(Duration::from_millis(50));
    }

    result
}

/// Object that tracks an in-progress dm-snapshot merge.
struct DmSnapshotMerge {
    pub name: String,
    pub complete: bool,
    snapshot_name: String,
    origin_majmin: String,
    start: Instant,
    starting_sectors: i64,
    error: bool,
}

impl DmSnapshotMerge {
    // Begin the process of merging a given snapshot into its origin. Returns
    // None if the given snapshot doesn't exist.
    pub fn new(name: &str) -> Result<Option<Self>> {
        let origin_name = format!("{}-origin", name);
        let snapshot_name = format!("{}-rw", name);
        let start = Instant::now();

        // If the snapshot path doesn't exist, there's nothing to merge.
        // Consider this a success.
        let snapshot_path = Path::new("/dev/mapper").join(&snapshot_name);
        if !snapshot_path.exists() {
            return Ok(None);
        }

        // Get the count of data sectors in the snapshot for later I/O rate
        // logging.
        let starting_sectors =
            get_snapshot_data_sectors(&snapshot_name).context("Failed to get snapshot size")?;

        // Get the origin table, which points at the "real" block device, for
        // later.
        let origin_table = get_dm_table(&origin_name)?;

        // Get the snapshot table line, and substitute snapshot for
        // snapshot-merge, which (once installed) will kick off the merge
        // process in the kernel.
        let snapshot_table = get_dm_table(&snapshot_name)?;
        let snapshot_table = snapshot_table.replace(" snapshot ", " snapshot-merge ");

        // Suspend both the origin and the snapshot. Be careful, as the stateful
        // volumes may now hang if written to (by loggers, for example). The
        // SuspendedDmDevice objects ensure the devices get resumed even if this
        // function bails out early.
        let suspended_origin =
            SuspendedDmDevice::new(&origin_name).context("Failed to suspend origin")?;
        let suspended_snapshot =
            SuspendedDmDevice::new(&snapshot_name).context("Failed to suspend snapshot")?;

        // With both devices suspended, replace the table to begin the merge process.
        reload_dm_table(&snapshot_name, &snapshot_table)
            .context("Failed to reload snapshot as merge")?;

        // If that worked, resume the devices (by dropping the suspend object),
        // then remove the origin.
        drop(suspended_origin);
        drop(suspended_snapshot);
        remove_dm_target(&origin_name).context("Failed to remove origin")?;

        // Delete the loop device backing the snapshot.
        let snapshot_majmin = get_nth_element(&snapshot_table, 4)?;
        if let Some(loop_path) = majmin_to_loop_path(snapshot_majmin) {
            delete_loop_device(&loop_path).context("Failed to delete loop device")?;
        } else {
            warn!("Warning: Underlying device for dm target {} is not a loop device, skipping loop deletion",
                  snapshot_majmin);
        }

        let origin_majmin = get_nth_element(&origin_table, 3)?.to_string();
        Ok(Some(Self {
            name: name.to_string(),
            start,
            snapshot_name,
            origin_majmin,
            starting_sectors,
            complete: false,
            error: false,
        }))
    }

    /// Check on the progress of the async merge happening. On success, returns
    /// the number of sectors remaining to merge.
    pub fn check_merge_progress(&mut self) -> Result<i64> {
        if self.complete {
            return Ok(0);
        }

        let result = self.check_and_complete_merge();
        if result.is_err() {
            self.error = true;
        }

        result
    }

    /// Inner routine to check the merge
    fn check_and_complete_merge(&mut self) -> Result<i64> {
        let data_sectors = get_snapshot_data_sectors(&self.snapshot_name)?;
        if data_sectors == 0 {
            self.complete_post_merge()?;
            self.complete = true;
        }

        Ok(data_sectors)
    }

    /// Perform the post-merge dm-table operations to convert the merged
    /// snapshot to a snapshot origin.
    fn complete_post_merge(&mut self) -> Result<()> {
        // Now that the snapshot is fully synced back into the origin, replace
        // that entry with a snapshot-origin for the "real" underlying physical
        // block device. This is done so the copy-on-write store can be released
        // and deleted if needed.
        //
        // For those wondering what happens to writes that occur after the "wait
        // for merge" is complete but before the device is suspended below:
        // future writes to to the merging snapshot go straight down to the disk
        // if they were clean in the merging snapshot. So the amount of data in
        // the snapshot only ever shrinks, it doesn't continue to grow once it's
        // begun merging. Since the disk now sees every write passing through,
        // this switcharoo doesn't create any inconsistencies.
        let suspended_snapshot =
            SuspendedDmDevice::new(&self.snapshot_name).context("Failed to suspend snapshot")?;

        let snapshot_table = get_dm_table(&self.snapshot_name)?;
        let origin_size = get_nth_element(&snapshot_table, 1)?;
        let origin_table = format!("0 {} snapshot-origin {}", origin_size, self.origin_majmin);
        reload_dm_table(&self.snapshot_name, &origin_table)
            .context("Failed to reload snapshot as origin")?;

        drop(suspended_snapshot);

        // Rename the dm target, which doesn't serve any functional purpose, but
        // serves as a useful breadcrumb during debugging and in feedback reports.
        let merged_name = format!("{}-merged", self.name);
        rename_dm_target(&self.snapshot_name, &merged_name)
            .context("Failed to rename dm target")?;

        // With the snapshot totally merged, delete the file backing it.
        delete_snapshot(&self.name)?;
        // Victory log!
        log_io_duration(
            &format!("Merged {} snapshot", &self.snapshot_name),
            self.starting_sectors * 512,
            self.start.elapsed(),
        );

        Ok(())
    }
}

impl Drop for DmSnapshotMerge {
    fn drop(&mut self) {
        // Print an error if the caller never waited for this merge.
        if !self.complete && !self.error {
            error!("Never waited on merge for {}", self.name);
        }
    }
}

/// Return the number of data sectors in the snapshot.
/// See https://www.kernel.org/doc/Documentation/device-mapper/snapshot.txt
fn get_snapshot_data_sectors(name: &str) -> Result<i64> {
    let status = get_dm_status(name).context("Failed to get dm status")?;
    let allocated_element =
        get_nth_element(&status, 3).context("Failed to get dm status allocated element")?;

    let allocated = allocated_element.split('/').next().unwrap();
    let metadata =
        get_nth_element(&status, 4).context("Failed to get dm status metadata element")?;

    let allocated: i64 = allocated
        .parse()
        .context("Failed to convert dm status allocated to i64")?;

    let metadata: i64 = metadata
        .parse()
        .context("Failed to convert dm status metadata to i64")?;

    Ok(allocated - metadata)
}

/// Delete a snapshot
fn delete_snapshot(name: &str) -> Result<()> {
    let snapshot_file_path = snapshot_file_path(name);
    info!("Deleting {}", snapshot_file_path.display());
    remove_file(snapshot_file_path).context("Failed to delete snapshot file")
}

/// Get the dm table line for a particular target.
fn get_dm_table(target: &str) -> Result<String> {
    dmsetup_checked_output(Command::new(DMSETUP_PATH).args(["table", target]))
        .context(format!("Failed to get dm target line for {}", target))
}

/// Get the dm status line for a particular target.
fn get_dm_status(target: &str) -> Result<String> {
    dmsetup_checked_output(Command::new(DMSETUP_PATH).args(["status", target]))
        .context(format!("Failed to get dm status line for {}", target))
}

/// Reload a target table.
fn reload_dm_table(target: &str, table: &str) -> Result<()> {
    checked_command(Command::new(DMSETUP_PATH).args(["reload", target, "--table", table])).context(
        format!(
            "Failed to run dmsetup reload for {}, table {}",
            target, table
        ),
    )
}

/// Rename a dm target
fn rename_dm_target(target: &str, new_name: &str) -> Result<()> {
    checked_command(Command::new(DMSETUP_PATH).args(["rename", target, new_name])).context(format!(
        "Failed to run dmsetup rename {} {}",
        target, new_name
    ))
}

/// Remove a dm target
fn remove_dm_target(target: &str) -> Result<()> {
    checked_command(Command::new(DMSETUP_PATH).args(["remove", target]))
        .context(format!("Failed to run dmsetup remove {}", target))
}

/// Delete a loop device.
fn delete_loop_device(dev: &str) -> Result<()> {
    checked_command(Command::new(LOSETUP_PATH).args(["-d", dev]))
        .context(format!("Failed to delete loop device: {}", dev))
}

/// Run a dmsetup command and return the output as a trimmed String.
fn dmsetup_checked_output(command: &mut Command) -> Result<String> {
    let output =
        checked_command_output(command).context("Failed to run dmsetup and collect output")?;

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Get a loop device path like /dev/loop4 out of a major:minor string like
/// 7:4
fn majmin_to_loop_path(majmin: &str) -> Option<String> {
    if !majmin.starts_with("7:") {
        return None;
    }

    let mut split = majmin.split(':');
    split.next();
    let loop_num: i32 = split.next().unwrap().parse().unwrap();
    Some(format!("/dev/loop{}", loop_num))
}

/// Return the file path backing the loop device backing a dm-snapshot
/// region for the given name.
fn snapshot_file_path(name: &str) -> PathBuf {
    snapshot_dir().join(name)
}

/// Return the snapshot directory.
fn snapshot_dir() -> PathBuf {
    Path::new(HIBERNATE_DIR).to_path_buf()
}

/// Separate a string by whitespace, and return the nth element, or an error
/// if the string doesn't contain that many elements.
fn get_nth_element(s: &str, n: usize) -> Result<&str> {
    let elements: Vec<&str> = s.split_whitespace().collect();
    if elements.len() <= n {
        return Err(HibernateError::IndexOutOfRangeError())
            .context(format!("Failed to get element {} in {}", n, s));
    }

    Ok(elements[n])
}
