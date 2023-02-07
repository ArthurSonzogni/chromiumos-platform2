// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements common functions and definitions used throughout the app and library.

use std::convert::TryInto;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::exit;
use std::process::Command;
use std::str;
use std::time::Duration;

use anyhow::Context;
use anyhow::Result;
use log::debug;
use log::error;
use log::info;
use log::warn;
use thiserror::Error as ThisError;

use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::hiberlog::redirect_log;
use crate::hiberlog::HiberlogOut;
use crate::metrics::MetricsLogger;
use crate::metrics::MetricsSample;
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
    FallocateError(libchromeos::sys::Error),
    /// Error getting the fiemap
    #[error("Error getting the fiemap: {0}")]
    FiemapError(libchromeos::sys::Error),
    /// First data byte mismatch
    #[error("First data byte mismatch")]
    FirstDataByteMismatch(),
    /// Insufficient Memory available.
    #[error("Not enough free memory and swap")]
    InsufficientMemoryAvailableError(),
    /// Invalid fiemap
    #[error("Invalid fiemap: {0}")]
    InvalidFiemapError(String),
    /// Key manager error
    #[error("Key manager error: {0}")]
    KeyManagerError(String),
    /// Metadata error
    #[error("Metadata error: {0}")]
    MetadataError(String),
    /// Failed to send metrics
    #[error("Failed to send metrics: {0}")]
    MetricsSendFailure(String),
    /// Failed to lock process memory.
    #[error("Failed to mlockall: {0}")]
    MlockallError(libchromeos::sys::Error),
    /// Mmap error.
    #[error("mmap error: {0}")]
    MmapError(libchromeos::sys::Error),
    /// I/O size error
    #[error("I/O size error: {0}")]
    IoSizeError(String),
    /// Snapshot device error.
    #[error("Snapshot device error: {0}")]
    SnapshotError(String),
    /// Snapshot ioctl error.
    #[error("Snapshot ioctl error: {0}: {1}")]
    SnapshotIoctlError(String, libchromeos::sys::Error),
    /// Mount not found.
    #[error("Mount not found")]
    MountNotFoundError(),
    /// Swap information not found.
    #[error("Swap information not found")]
    SwapInfoNotFoundError(),
    /// Failed to shut down
    #[error("Failed to shut down: {0}")]
    ShutdownError(libchromeos::sys::Error),
    /// End of file
    #[error("End of file")]
    EndOfFile(),
    /// Hibernate volume error
    #[error("Hibernate volume error")]
    HibernateVolumeError(),
    /// Spawned process error
    #[error("Spawned process error: {0}")]
    SpawnedProcessError(i32),
    /// Index out of range error
    #[error("Index out of range")]
    IndexOutOfRangeError(),
    /// Resume abort requested
    #[error("Resume abort requested: {0}")]
    ResumeAbortRequested(String),
    /// Device mapper error
    #[error("Device mapper error: {0}")]
    DeviceMapperError(String),
    /// Merge timeout error
    #[error("Merge timeout error")]
    MergeTimeoutError(),
    /// Update engine busy error
    #[error("Update engine busy")]
    UpdateEngineBusyError(),
}

/// Options taken from the command line affecting hibernate.
#[derive(Default)]
pub struct HibernateOptions {
    pub dry_run: bool,
}

/// Options taken from the command line affecting resume-init.
#[derive(Default)]
pub struct ResumeInitOptions {
    pub force: bool,
}

/// Options taken from the command line affecting resume.
#[derive(Default)]
pub struct ResumeOptions {
    pub dry_run: bool,
}

/// Options taken from the command line affecting abort-resume.
pub struct AbortResumeOptions {
    pub reason: String,
}

impl Default for AbortResumeOptions {
    fn default() -> Self {
        Self {
            reason: "Manually aborted by hiberman abort-resume".to_string(),
        }
    }
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
pub fn prealloc_mem(metrics_logger: &mut MetricsLogger) -> Result<()> {
    let available_mb = get_available_memory_mb();
    let available_swap = get_available_swap_mb()?;
    let total_avail = available_mb + available_swap;
    debug!(
        "System has {} MB of free memory, {} MB of swap free",
        available_mb, available_swap
    );
    let memory_pages = get_total_memory_pages();
    let hiber_pages = memory_pages / 2;
    let page_size = get_page_size();
    let hiber_size = page_size * hiber_pages;
    let hiber_mb = hiber_size / (1024 * 1024);
    let mut extra_mb = (available_mb + available_swap) as isize - hiber_mb as isize;
    let mut shortfall_mb = 0;
    if extra_mb < 0 {
        shortfall_mb = -extra_mb;
        extra_mb = 0;
    }
    metrics_logger.log_metric(MetricsSample {
        name: "Platform.Hibernate.MemoryAvailable",
        value: available_mb as isize,
        min: 0,
        max: 16384,
        buckets: 50,
    });
    metrics_logger.log_metric(MetricsSample {
        name: "Platform.Hibernate.MemoryAndSwapAvailable",
        value: total_avail as isize,
        min: 0,
        max: 32768,
        buckets: 50,
    });
    metrics_logger.log_metric(MetricsSample {
        name: "Platform.Hibernate.AdditionalMemoryNeeded",
        value: shortfall_mb,
        min: 0,
        max: 16384,
        buckets: 50,
    });
    metrics_logger.log_metric(MetricsSample {
        name: "Platform.Hibernate.ExcessMemoryAvailable",
        value: extra_mb,
        min: 0,
        max: 16384,
        buckets: 50,
    });

    if hiber_mb > (total_avail).try_into().unwrap() {
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

/// Look through /proc/mounts to find the block device supporting the
/// given directory. The directory must be the root of a mount.
pub fn get_device_mounted_at_dir(mount_path: &str) -> Result<String> {
    // Go look through the mounts to see where the given mount is.
    let f = File::open("/proc/mounts")?;
    let buf_reader = BufReader::new(f);
    for line in buf_reader.lines().flatten() {
        let mut split = line.split_whitespace();
        let blk = split.next();
        let path = split.next();
        if let Some(path) = path {
            if path == mount_path {
                if let Some(blk) = blk {
                    return Ok(blk.to_string());
                }
            }
        }
    }

    Err(HibernateError::MountNotFoundError())
        .context(format!("Failed to find mount at {}", mount_path))
}

/// Return the path to partition one (stateful) on the root block device.
pub fn stateful_block_partition_one() -> Result<String> {
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
    let output = checked_command_output(Command::new("/usr/bin/rootdev").args(["-d", "-s"]))
        .context("Cannot get rootdev")?;
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Determines if the stateful-rw snapshot is active, indicating a resume boot.
pub fn is_snapshot_active() -> bool {
    fs::metadata("/dev/mapper/stateful-rw").is_ok()
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
        return Err(HibernateError::MlockallError(
            libchromeos::sys::Error::last(),
        ))
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

/// Wait for a std::process::Command, and convert the exit status into a Result
pub fn checked_command(command: &mut std::process::Command) -> Result<()> {
    let mut child = command.spawn().context("Failed to spawn child process")?;
    let exit_status = child.wait().context("Failed to wait for child")?;
    if exit_status.success() {
        Ok(())
    } else {
        let code = exit_status.code().unwrap_or(-2);
        Err(HibernateError::SpawnedProcessError(code)).context(format!(
            "Command {} failed with code {}",
            command.get_program().to_string_lossy(),
            &code
        ))
    }
}

/// Wait for a std::process::Command, convert its exit status into a Result, and
/// collect the output on success.
pub fn checked_command_output(command: &mut std::process::Command) -> Result<std::process::Output> {
    let output = command
        .output()
        .context("Failed to get output for child process")?;
    let exit_status = output.status;
    if exit_status.success() {
        Ok(output)
    } else {
        let code = exit_status.code().unwrap_or(-2);
        Err(HibernateError::SpawnedProcessError(code)).context(format!(
            "Command {} failed with code {}",
            command.get_program().to_string_lossy(),
            &code
        ))
    }
}

/// Perform emergency bailout procedures (like syncing logs), set the cookie to
/// indicate something went very wrong, and reboot the system.
pub fn emergency_reboot(reason: &str) {
    error!("Performing emergency reboot: {}", reason);
    // Attempt to set the cookie, but do not stop if it fails.
    if let Err(e) = set_hibernate_cookie::<PathBuf>(None, HibernateCookieValue::EmergencyReboot) {
        error!("Failed to set cookie to EmergencyReboot: {}", e);
    }
    // Redirect the log to in-memory, which flushes out any pending logs if
    // logging is already directed to a file.
    redirect_log(HiberlogOut::BufferInMemory);
    reboot_system().unwrap();
    // Exit with a weird error code to avoid going through this path multiple
    // times.
    exit(9);
}

/// Perform an orderly reboot.
fn reboot_system() -> Result<()> {
    error!("Rebooting system!");
    checked_command(&mut Command::new("/sbin/reboot")).context("Failed to reboot system")
}

/// Invoke initctl to fire off an upstart event.
pub fn emit_upstart_event(name: &str) -> Result<()> {
    debug!("Emitting upstart event: {}", name);
    checked_command(Command::new("/sbin/initctl").args(["emit", "--no-wait", name]))
        .context("Failed to create initctl command")
}
