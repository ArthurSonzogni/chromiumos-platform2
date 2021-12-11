// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! High level support for creating and opening the files used by hibernate.

use std::convert::TryInto;
use std::fs::{create_dir, File, OpenOptions};
use std::os::unix::io::AsRawFd;
use std::path::Path;

use anyhow::{Context, Result};
use log::{debug, info};

use crate::diskfile::{BouncedDiskFile, DiskFile};
use crate::hiberlog::HiberlogFile;
use crate::hiberutil::{get_page_size, get_total_memory_pages, HibernateError};
use crate::splitter::HIBER_HEADER_MAX_SIZE;

/// Define the directory where hibernate state files are kept.
pub const HIBERNATE_DIR: &str = "/mnt/stateful_partition/unencrypted/hibernate";
/// Define the name of the hibernate metadata.
const HIBER_META_NAME: &str = "metadata";
/// Define the preallocated size of the hibernate metadata file.
const HIBER_META_SIZE: i64 = 1024 * 1024 * 8;
/// Define the name of the header pages file.
const HIBER_HEADER_NAME: &str = "header";
/// Define the name of the main hibernate image data file.
const HIBER_DATA_NAME: &str = "hiberfile";
/// Define the name of the resume log file.
const RESUME_LOG_FILE_NAME: &str = "resume_log";
/// Define the name of the suspend log file.
const SUSPEND_LOG_FILE_NAME: &str = "suspend_log";
/// Define the size of the preallocated log files.
const HIBER_LOG_SIZE: i64 = 1024 * 1024 * 4;

/// Create the hibernate directory if it does not exist.
pub fn create_hibernate_dir() -> Result<()> {
    if !Path::new(HIBERNATE_DIR).exists() {
        debug!("Creating hibernate directory");
        create_dir(HIBERNATE_DIR).context("Failed to create hibernate directory")?;
    }

    Ok(())
}

/// Preallocates the metadata file and opens it for I/O.
pub fn preallocate_metadata_file() -> Result<BouncedDiskFile> {
    let metadata_path = Path::new(HIBERNATE_DIR).join(HIBER_META_NAME);
    let mut meta_file = preallocate_file(&metadata_path, HIBER_META_SIZE)?;
    BouncedDiskFile::new(&mut meta_file, None)
}

/// Preallocate and open the suspend or resume log file.
pub fn preallocate_log_file(log_file: HiberlogFile) -> Result<BouncedDiskFile> {
    let name = match log_file {
        HiberlogFile::Suspend => SUSPEND_LOG_FILE_NAME,
        HiberlogFile::Resume => RESUME_LOG_FILE_NAME,
    };

    let log_file_path = Path::new(HIBERNATE_DIR).join(name);
    let mut log_file = preallocate_file(&log_file_path, HIBER_LOG_SIZE)?;
    BouncedDiskFile::new(&mut log_file, None)
}

/// Preallocate the header pages file.
pub fn preallocate_header_file() -> Result<DiskFile> {
    let path = Path::new(HIBERNATE_DIR).join(HIBER_HEADER_NAME);
    let mut file = preallocate_file(&path, HIBER_HEADER_MAX_SIZE)?;
    DiskFile::new(&mut file, None)
}

/// Preallocate the hibernate image file.
pub fn preallocate_hiberfile() -> Result<DiskFile> {
    let hiberfile_path = Path::new(HIBERNATE_DIR).join(HIBER_DATA_NAME);

    // The maximum size of the hiberfile is half of memory, plus a little
    // fudge for rounding. KASAN steals 1/8 of memory if it's enabled and makes
    // it look invisible, but still needs to be saved, so multiply by 8/7 to
    // account for the rare debug case where it's enabled.
    let memory_mb = get_total_memory_mb();
    let hiberfile_mb = ((memory_mb * 8 / 7) / 2) + 2;
    debug!(
        "System has {} MB of memory, preallocating {} MB hiberfile",
        memory_mb, hiberfile_mb
    );

    let hiber_size = (hiberfile_mb as i64) * 1024 * 1024;
    let mut hiber_file = preallocate_file(&hiberfile_path, hiber_size)?;
    info!("Successfully preallocated {} MB hiberfile", hiberfile_mb);
    DiskFile::new(&mut hiber_file, None)
}

/// Open a pre-existing disk file with bounce buffer,
/// still with read and write permissions.
pub fn open_bounced_disk_file<P: AsRef<Path>>(path: P) -> Result<BouncedDiskFile> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .context("Cannot open bounced disk file")?;
    BouncedDiskFile::new(&mut file, None)
}

/// Open a pre-existing header file, still with read and write permissions.
pub fn open_header_file() -> Result<DiskFile> {
    let path = Path::new(HIBERNATE_DIR).join(HIBER_HEADER_NAME);
    open_disk_file(&path)
}

/// Open a pre-existing hiberfile, still with read and write permissions.
pub fn open_hiberfile() -> Result<DiskFile> {
    let hiberfile_path = Path::new(HIBERNATE_DIR).join(HIBER_DATA_NAME);
    open_disk_file(&hiberfile_path)
}

/// Open a pre-existing hiberfile, still with read and write permissions.
pub fn open_metafile() -> Result<BouncedDiskFile> {
    let hiberfile_path = Path::new(HIBERNATE_DIR).join(HIBER_META_NAME);
    open_bounced_disk_file(&hiberfile_path)
}

/// Open one of the log files, either the suspend or resume log.
pub fn open_log_file(log_file: HiberlogFile) -> Result<BouncedDiskFile> {
    let name = match log_file {
        HiberlogFile::Suspend => SUSPEND_LOG_FILE_NAME,
        HiberlogFile::Resume => RESUME_LOG_FILE_NAME,
    };

    let path = Path::new(HIBERNATE_DIR).join(name);
    open_bounced_disk_file(&path)
}

/// Helper function to get the total amount of physical memory on this system,
/// in megabytes.
fn get_total_memory_mb() -> u32 {
    let pagesize = get_page_size() as u64;
    let pagecount = get_total_memory_pages() as u64;

    debug!("Pagesize {} pagecount {}", pagesize, pagecount);
    let mb = pagecount * pagesize / (1024 * 1024);
    mb.try_into().unwrap_or(u32::MAX)
}

/// Helper function used to preallocate space on a file using the fallocate64()
/// C library call.
fn preallocate_file<P: AsRef<Path>>(path: P, size: i64) -> Result<File> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(path)
        .context("Failed to preallocate hibernate file")?;

    // This is safe because fallocate64() doesn't modify memory.
    let rc = unsafe { libc::fallocate64(file.as_raw_fd(), 0, 0, size) as isize };

    if rc < 0 {
        return Err(HibernateError::FallocateError(sys_util::Error::last()))
            .context("Failed to preallocate via fallocate");
    }

    Ok(file)
}

/// Open a pre-existing disk file, still with read and write permissions.
fn open_disk_file<P: AsRef<Path>>(path: P) -> Result<DiskFile> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .context("Failed to open disk file")?;
    DiskFile::new(&mut file, None)
}
