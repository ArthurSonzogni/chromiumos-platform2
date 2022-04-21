// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement common functions and definitions used throughout the app and library.

use std::convert::TryInto;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::process::Command;
use std::str;
use std::time::Duration;

use anyhow::{Context, Result};
use log::{debug, error, info, warn};
use thiserror::Error as ThisError;

use crate::mmapbuf::MmapBuffer;

/// Define the number of pages in a larger chunk used to read and write the
/// hibernate data file.
pub const BUFFER_PAGES: usize = 32;

#[derive(Debug, ThisError)]
pub enum HibernateError {
    /// Cookie error
    #[error("Cookie error: {0}")]
    CookieError(String),
    /// Dbus error
    #[error("Dbus error: {0}")]
    DbusError(String),
    /// Failed to copy the FD for the polling context.
    #[error("Failed to fallocate the file: {0}")]
    FallocateError(sys_util::Error),
    /// Error getting the fiemap
    #[error("Error getting the fiemap: {0}")]
    FiemapError(sys_util::Error),
    /// First data byte mismatch
    #[error("First data byte mismatch")]
    FirstDataByteMismatch(),
    /// Header content hash mismatch
    #[error("Header content hash mismatch")]
    HeaderContentHashMismatch(),
    /// Header incomplete
    #[error("Header incomplete")]
    HeaderIncomplete(),
    /// Insufficient Memory available.
    #[error("Not enough free memory and swap")]
    InsufficientMemoryAvailableError(),
    /// Invalid fiemap
    #[error("Invalid fiemap: {0}")]
    InvalidFiemapError(String),
    /// Image unencrypted
    #[error("Image unencrypted")]
    ImageUnencryptedError(),
    /// Key manager error
    #[error("Key manager error: {0}")]
    KeyManagerError(String),
    /// Metadata error
    #[error("Metadata error: {0}")]
    MetadataError(String),
    /// Failed to lock process memory.
    #[error("Failed to mlockall: {0}")]
    MlockallError(sys_util::Error),
    /// Mmap error.
    #[error("mmap error: {0}")]
    MmapError(sys_util::Error),
    /// I/O size error
    #[error("I/O size error: {0}")]
    IoSizeError(String),
    /// Snapshot device error.
    #[error("Snapshot device error: {0}")]
    SnapshotError(String),
    /// Snapshot ioctl error.
    #[error("Snapshot ioctl error: {0}: {1}")]
    SnapshotIoctlError(String, sys_util::Error),
    /// Stateful partition not found.
    #[error("Stateful partition not found")]
    StatefulPartitionNotFoundError(),
    /// Swap information not found.
    #[error("Swap information not found")]
    SwapInfoNotFoundError(),
    /// Failed to shut down
    #[error("Failed to shut down: {0}")]
    ShutdownError(sys_util::Error),
}

/// Options taken from the command line affecting hibernate.
#[derive(Default)]
pub struct HibernateOptions {
    pub dry_run: bool,
    pub unencrypted: bool,
    pub test_keys: bool,
    pub force_platform_mode: bool,
}

/// Options taken from the command line affecting resume.
#[derive(Default)]
pub struct ResumeOptions {
    pub dry_run: bool,
    pub unencrypted: bool,
    pub test_keys: bool,
    pub no_preloader: bool,
}

/// Get the page size on this system.
pub fn get_page_size() -> usize {
    // Safe because sysconf() returns a long and has no other side effects.
    unsafe { libc::sysconf(libc::_SC_PAGESIZE) as usize }
}

/// Get the amount of free memory (in pages) on this system.
pub fn get_available_pages() -> usize {
    // Safe because sysconf() returns a long and has no other side effects.
    unsafe { libc::sysconf(libc::_SC_AVPHYS_PAGES) as usize }
}

/// Get the total amount of memory (in pages) on this system.
pub fn get_total_memory_pages() -> usize {
    // Safe because sysconf() returns a long and has no other side effects.
    let pagecount = unsafe { libc::sysconf(libc::_SC_PHYS_PAGES) as usize };
    if pagecount == 0 {
        warn!(
            "Failed to get total memory (got {}). Assuming 4GB.",
            pagecount
        );
        // Just return 4GB worth of pages if the result is unknown, the minimum
        // we're ever going to see on a hibernating system.
        let pages_per_mb = 1024 * 1024 / get_page_size();
        let pages_per_gb = pages_per_mb * 1024;
        return pages_per_gb * 4;
    }

    pagecount
}

/// Helper function to get the amount of free physical memory on this system,
/// in megabytes.
fn get_available_memory_mb() -> u32 {
    let pagesize = get_page_size() as u64;
    let pagecount = get_available_pages() as u64;

    let mb = pagecount * pagesize / (1024 * 1024);
    mb.try_into().unwrap_or(u32::MAX)
}

// Helper function to get the amount of free swap on this system.
fn get_available_swap_mb() -> Result<u32> {
    // Look in /proc/meminfo to find swap info.
    let f = File::open("/proc/meminfo")?;
    let buf_reader = BufReader::new(f);
    for line in buf_reader.lines().flatten() {
        let mut split = line.split_whitespace();
        let arg = split.next();
        let value = split.next();
        if let Some(arg) = arg {
            if arg == "SwapFree:" {
                if let Some(value) = value {
                    let swap_free: u32 = value.parse().context("Failed to parse SwapFree value")?;
                    let mb: u32 = swap_free / 1024;
                    return Ok(mb);
                }
            }
        }
    }
    Err(HibernateError::SwapInfoNotFoundError())
        .context("Failed to find available swap information")
}

// Preallocate memory that will be needed for the hibernate snapshot.
// Currently the kernel is not always able to reclaim memory effectively
// when allocating the memory needed for the hibernate snapshot. By
// preallocating this memory, we force memory to be swapped into zram and
// ensure that we have the free memory needed for the snapshot.
pub fn prealloc_mem() -> Result<()> {
    let available_mb = get_available_memory_mb();
    let available_swap = get_available_swap_mb()?;
    debug!(
        "System has {} MB of free memory, {} MB of swap free",
        available_mb, available_swap
    );
    let memory_pages = get_total_memory_pages();
    let hiber_pages = memory_pages / 2;
    let page_size = get_page_size();
    let hiber_size = page_size * hiber_pages;
    let hiber_mb = hiber_size / (1024 * 1024);
    if hiber_mb > (available_mb + available_swap).try_into().unwrap() {
        return Err(HibernateError::InsufficientMemoryAvailableError())
            .context("Not enough free memory and swap space for hibernate");
    }
    debug!(
        "System has {} pages of memory, preallocating {} pages for hibernate",
        memory_pages, hiber_pages
    );

    let mut buffer =
        MmapBuffer::new(hiber_size).context("Failed to create buffer for memory allocation")?;
    let buf = buffer.u8_slice_mut();
    let mut i = 0;
    while i < buf.len() {
        buf[i] = 0;
        i += page_size
    }

    let available_mb_after = get_available_memory_mb();
    let available_swap_after = get_available_swap_mb()?;
    debug!(
        "System has {} MB of free memory, {} MB of free swap after giant allocation",
        available_mb_after, available_swap_after
    );

    drop(buffer);
    let available_mb_final = get_available_memory_mb();
    let available_swap_final = get_available_swap_mb()?;
    debug!(
        "System has {} MB of free memory, {} MB of free swap after freeing giant allocation",
        available_mb_final, available_swap_final
    );
    Ok(())
}

/// Returns the block device that the unencrypted stateful file system resides
/// on. This function always returns the true RW block device, even if stateful
/// is actually mounted on a dm-snapshot device where writes are being diverted.
/// This is needed so that hibernate logs and data can be persisted across a
/// successful resume.
pub fn path_to_stateful_part() -> Result<String> {
    let mounted_stateful = path_to_mounted_stateful_part()?;

    // If the snapshot is not active, just use the stateful mount block device
    // directly.
    if !is_snapshot_active() {
        return Ok(mounted_stateful);
    }

    // Ok, so there is a snapshot active. Try to get the volume group name for
    // the stateful partition (by going directly up from the physical block
    // device, rather than down from the mount). This is also a test of whether
    // or not we're on an LVM-enabled system. If we fail to get the VG name,
    // this must not be an LVM-enabled system, so just return partition one.
    let partition1 = stateful_block_partition_one()?;
    let vg_name = match get_vg_name(&partition1) {
        Ok(vg) => vg,
        Err(_) => {
            return Ok(partition1);
        }
    };

    Ok(format!("/dev/{}/unencrypted", vg_name))
}

/// Helper function to determine if this is a system where the stateful
/// partition is running on top of LVM.
pub fn is_lvm_system() -> Result<bool> {
    let partition1 = stateful_block_partition_one()?;
    let mut file = File::open(&partition1)?;
    let mut buffer = MmapBuffer::new(4096)?;
    let buf = buffer.u8_slice_mut();
    file.read_exact(buf)
        .context(format!("Failed to read {}", partition1))?;
    // LVM systems have a Physical Volume Label header that starts with
    // "LABELONE" as its magic. If that's found, this is an LVM system.
    // https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/4/html/cluster_logical_volume_manager/lvm_metadata
    match str::from_utf8(&buf[512..520]) {
        Ok(l) => Ok(l == "LABELONE"),
        Err(_) => Ok(false),
    }
}

/// Look through /proc/mounts to find the block device supporting the
/// unencrypted stateful partition.
fn path_to_mounted_stateful_part() -> Result<String> {
    // Go look through the mounts to see where /mnt/stateful_partition is.
    let f = File::open("/proc/mounts")?;
    let buf_reader = BufReader::new(f);
    for line in buf_reader.lines().flatten() {
        let mut split = line.split_whitespace();
        let blk = split.next();
        let path = split.next();
        if let Some(path) = path {
            if path == "/mnt/stateful_partition" {
                if let Some(blk) = blk {
                    return Ok(blk.to_string());
                }
            }
        }
    }

    Err(HibernateError::StatefulPartitionNotFoundError())
        .context("Failed to find mounted stateful partition")
}

/// Return the path to partition one (stateful) on the root block device.
fn stateful_block_partition_one() -> Result<String> {
    let rootdev = path_to_stateful_block()?;
    let last = rootdev.chars().last();
    if let Some(last) = last {
        if last.is_numeric() {
            return Ok(format!("{}p1", rootdev));
        }
    }

    Ok(format!("{}1", rootdev))
}

/// Determine the path to the block device containing the stateful partition.
/// Farm this out to rootdev to keep the magic in one place.
pub fn path_to_stateful_block() -> Result<String> {
    let output = Command::new("/usr/bin/rootdev")
        .arg("-d")
        .output()
        .context("Cannot get rootdev")?;
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Get the volume group name for the stateful block device.
fn get_vg_name(blockdev: &str) -> Result<String> {
    let output = Command::new("/sbin/pvdisplay")
        .args(["-C", "--noheadings", "-o", "vg_name", blockdev])
        .output()
        .context("Cannot run pvdisplay to get volume group name")?;

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Determines if the stateful-rw snapshot is active, indicating a resume boot.
fn is_snapshot_active() -> bool {
    fs::metadata("/dev/mapper/stateful-rw").is_ok()
}

pub struct ActivatedVolumeGroup {
    vg_name: Option<String>,
}

impl ActivatedVolumeGroup {
    fn new(vg_name: String) -> Result<Self> {
        // If it already exists, don't reactivate it.
        if fs::metadata(format!("/dev/{}/unencrypted", vg_name)).is_ok() {
            return Ok(Self { vg_name: None });
        }

        Command::new("/sbin/vgchange")
            .args(["-ay", &vg_name])
            .output()
            .context("Cannot activate volume group")?;

        Ok(Self {
            vg_name: Some(vg_name),
        })
    }
}

impl Drop for ActivatedVolumeGroup {
    fn drop(&mut self) {
        if let Some(vg_name) = &self.vg_name {
            let r = Command::new("/sbin/vgchange")
                .args(["-an", vg_name])
                .output();

            match r {
                Ok(_) => {
                    info!("Deactivated vg {}", vg_name);
                }
                Err(e) => {
                    warn!("Failed to deactivate VG {}: {}", vg_name, e);
                }
            }
        }
    }
}

pub fn activate_physical_vg() -> Result<Option<ActivatedVolumeGroup>> {
    if !is_snapshot_active() {
        return Ok(None);
    }

    let partition1 = stateful_block_partition_one()?;
    // Assume that a failure to get the VG name indicates a non-LVM system.
    let vg_name = match get_vg_name(&partition1) {
        Ok(vg) => vg,
        Err(_) => {
            return Ok(None);
        }
    };

    let vg = ActivatedVolumeGroup::new(vg_name)?;
    Ok(Some(vg))
}

pub struct LockedProcessMemory {}

impl Drop for LockedProcessMemory {
    fn drop(&mut self) {
        unlock_process_memory();
    }
}

/// Lock all present and future memory belonging to this process, preventing it
/// from being paged out. Returns a LockedProcessMemory token, which undoes the
/// operation when dropped.
pub fn lock_process_memory() -> Result<LockedProcessMemory> {
    // This is safe because mlockall() does not modify memory, it only ensures
    // it doesn't get swapped out, which maintains Rust's safety guarantees.
    let rc = unsafe { libc::mlockall(libc::MCL_CURRENT | libc::MCL_FUTURE) };

    if rc < 0 {
        return Err(HibernateError::MlockallError(sys_util::Error::last()))
            .context("Cannot lock process memory");
    }

    Ok(LockedProcessMemory {})
}

/// Unlock memory belonging to this process, allowing it to be paged out once
/// more.
fn unlock_process_memory() {
    // This is safe because while munlockall() is a foreign function, it has
    // no immediately observable side effects on program execution.
    unsafe {
        libc::munlockall();
    };
}

/// Log a duration with level info in the form: <action> in X.YYY seconds.
pub fn log_duration(action: &str, duration: Duration) {
    info!(
        "{} in {}.{:03} seconds",
        action,
        duration.as_secs(),
        duration.subsec_millis()
    );
}

/// Log a duration with an I/O rate at level info in the form:
/// <action> in X.YYY seconds, N bytes, A.BB MB/s.
pub fn log_io_duration(action: &str, io_bytes: i64, duration: Duration) {
    let rate = ((io_bytes as f64) / duration.as_secs_f64()) / 1048576.0;
    info!(
        "{} in {}.{:03} seconds, {} bytes, {:.3} MB/s",
        action,
        duration.as_secs(),
        duration.subsec_millis(),
        io_bytes,
        rate
    );
}
