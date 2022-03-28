// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement common functions and definitions used throughout the app and library.

use std::process::Command;

use anyhow::{Context, Result};
use log::{error, warn};
use thiserror::Error as ThisError;

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
    /// Header content length mismatch
    #[error("Header content length mismatch")]
    HeaderContentLengthMismatch(),
    /// Header incomplete
    #[error("Header incomplete")]
    HeaderIncomplete(),
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

/// Return the underlying partition device the hibernate files reside on.
/// Note: this still needs to return the real partition, even if stateful
/// is mounted on a dm-snapshot. Otherwise, resume activities won't work
/// across the transition.
pub fn path_to_stateful_part() -> Result<String> {
    let rootdev = path_to_stateful_block()?;
    Ok(format!("{}p1", rootdev))
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
