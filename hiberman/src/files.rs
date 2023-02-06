// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! High level support for creating and opening the files used by hibernate.

use std::fs::create_dir;
use std::fs::metadata;
use std::fs::remove_file;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Read;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::os::unix::io::AsRawFd;
use std::path::Path;

use anyhow::Context;
use anyhow::Result;
use log::warn;

use crate::diskfile::BouncedDiskFile;
use crate::diskfile::DiskFile;
use crate::hiberlog::HiberlogFile;
use crate::hiberutil::HibernateError;
use crate::metrics::MetricsFile;
use crate::mmapbuf::MmapBuffer;

/// Define the directory where hibernate state files are kept.
pub const HIBERNATE_DIR: &str = "/mnt/hibernate";
/// Define the root of the stateful partition mount.
pub const STATEFUL_DIR: &str = "/mnt/stateful_partition";
/// Define the ramfs location where ephemeral files are stored that should not
/// persist across even an unexpected reboot.
pub const TMPFS_DIR: &str = "/run/hibernate/";
/// Define the name of the token file indicating resume is in progress. Note:
/// Services outside of hiberman use this file, so don't change this name
/// carelessly.
const RESUME_IN_PROGRESS_FILE: &str = "resume_in_progress";
/// Define the name of the main hibernate image data file.
const HIBER_DATA_NAME: &str = "hiberfile";
/// Define the name of the resume log file.
const RESUME_LOG_FILE_NAME: &str = "resume_log";
/// Define the name of the suspend log file.
const SUSPEND_LOG_FILE_NAME: &str = "suspend_log";
/// Define the size of the preallocated log files.
const HIBER_LOG_SIZE: i64 = 1024 * 1024 * 4;
/// Define the attempts count file name.
const HIBER_ATTEMPTS_FILE_NAME: &str = "attempts_count";
/// Define the hibernate failures count file name.
const HIBER_FAILURES_FILE_NAME: &str = "hibernate_failures";
/// Define the resume failures count file name.
const RESUME_FAILURES_FILE_NAME: &str = "resume_failures";
/// Define the resume metrics file name.
const RESUME_METRICS_FILE_NAME: &str = "resume_metrics";
/// Define the suspend metrics file name.
const SUSPEND_METRICS_FILE_NAME: &str = "suspend_metrics";
/// Define the size of the preallocated metrics file.
const HIBER_METRICS_SIZE: i64 = 1024 * 1024 * 4;

/// Preallocate and open the suspend or resume log file.
pub fn preallocate_log_file(log_file: HiberlogFile, zero_out: bool) -> Result<BouncedDiskFile> {
    let name = match log_file {
        HiberlogFile::Suspend => SUSPEND_LOG_FILE_NAME,
        HiberlogFile::Resume => RESUME_LOG_FILE_NAME,
    };

    let log_file_path = Path::new(HIBERNATE_DIR).join(name);
    preallocate_bounced_disk_file(log_file_path, HIBER_LOG_SIZE, zero_out)
}

/// Preallocate the metrics file.
pub fn preallocate_metrics_file(
    metrics_file: MetricsFile,
    zero_out: bool,
) -> Result<BouncedDiskFile> {
    let name = match metrics_file {
        MetricsFile::Suspend => SUSPEND_METRICS_FILE_NAME,
        MetricsFile::Resume => RESUME_METRICS_FILE_NAME,
    };

    let metrics_file_path = Path::new(HIBERNATE_DIR).join(name);
    preallocate_bounced_disk_file(metrics_file_path, HIBER_METRICS_SIZE, zero_out)
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

/// Helper function to determine if the hiberfile already exists.
pub fn does_hiberfile_exist() -> bool {
    let hiberfile_path = Path::new(HIBERNATE_DIR).join(HIBER_DATA_NAME);
    metadata(hiberfile_path).is_ok()
}

/// Check if a metrics file exists.
pub fn metrics_file_exists(metrics_file: &MetricsFile) -> bool {
    let name = match metrics_file {
        MetricsFile::Suspend => SUSPEND_METRICS_FILE_NAME,
        MetricsFile::Resume => RESUME_METRICS_FILE_NAME,
    };

    let hiberfile_path = Path::new(HIBERNATE_DIR).join(name);
    hiberfile_path.exists()
}

/// Open a pre-existing metrics file with read and write permissions.
pub fn open_metrics_file(metrics_file: MetricsFile) -> Result<BouncedDiskFile> {
    let name = match metrics_file {
        MetricsFile::Suspend => SUSPEND_METRICS_FILE_NAME,
        MetricsFile::Resume => RESUME_METRICS_FILE_NAME,
    };

    let hiberfile_path = Path::new(HIBERNATE_DIR).join(name);
    open_bounced_disk_file(hiberfile_path)
}

/// Open one of the log files, either the suspend or resume log.
pub fn open_log_file(log_file: HiberlogFile) -> Result<BouncedDiskFile> {
    let name = match log_file {
        HiberlogFile::Suspend => SUSPEND_LOG_FILE_NAME,
        HiberlogFile::Resume => RESUME_LOG_FILE_NAME,
    };

    let path = Path::new(HIBERNATE_DIR).join(name);
    open_bounced_disk_file(path)
}

/// Open a metrics file.
fn open_cumulative_metrics_file(path: &Path) -> Result<File> {
    let file = File::options()
        .read(true)
        .write(true)
        .create(true)
        .open(path)
        .context("Cannot open metrics file")?;
    Ok(file)
}

/// Open the attempts_count file, to keep track of the number of hibernate
/// attempts for metric tracking purposes.
pub fn open_attempts_file() -> Result<File> {
    let path = Path::new(HIBERNATE_DIR).join(HIBER_ATTEMPTS_FILE_NAME);
    open_cumulative_metrics_file(&path)
}

/// Open the hibernate_failures file, to keep track of the number of hibernate
/// failures for metric tracking purposes.
pub fn open_hiber_fails_file() -> Result<File> {
    let path = Path::new(HIBERNATE_DIR).join(HIBER_FAILURES_FILE_NAME);
    open_cumulative_metrics_file(&path)
}

/// Open the resume_failures file, to keep track of the number of resume
/// failures for metric tracking purposes.
pub fn open_resume_failures_file() -> Result<File> {
    let path = Path::new(HIBERNATE_DIR).join(RESUME_FAILURES_FILE_NAME);
    open_cumulative_metrics_file(&path)
}

/// Read the given metrics file
pub fn read_metric_file(file: &mut File) -> Result<String> {
    let mut value_str = String::new();
    file.read_to_string(&mut value_str)
        .context("Failed to parse metric value")?;
    Ok(value_str)
}

/// Increment the value in the counter file
pub fn increment_file_counter(file: &mut File) -> Result<()> {
    let value_str = read_metric_file(file)?;
    let mut value: u32 = value_str.parse().unwrap_or(0);
    value += 1;
    file.rewind()?;
    file.write_all(value.to_string().as_bytes())
        .context("Failed to increment counter")
}

/// Add the resuming file token that other services can check to quickly see if
/// a resume is in progress.
pub fn create_resume_in_progress_file() -> Result<()> {
    if !Path::new(TMPFS_DIR).exists() {
        create_dir(TMPFS_DIR).context("Cannot create tmpfs directory")?;
    }

    let rip_path = Path::new(TMPFS_DIR).join(RESUME_IN_PROGRESS_FILE);
    if rip_path.exists() {
        warn!("{} unexpectedly already exists", rip_path.display());
    }

    OpenOptions::new()
        .write(true)
        .create(true)
        .open(rip_path)
        .context("Failed to create resume token file")?;

    Ok(())
}

/// Remove the resume_in_progress file if it exists. A result is not returned
/// because besides logging (done here) there's really no handling of this error
/// that could be done.
pub fn remove_resume_in_progress_file() {
    let rip_path = Path::new(TMPFS_DIR).join(RESUME_IN_PROGRESS_FILE);
    if rip_path.exists() {
        if let Err(e) = remove_file(&rip_path) {
            warn!("Failed to remove {}: {}", rip_path.display(), e);
            if rip_path.exists() {
                warn!("{} still exists!", rip_path.display());
            }
        }
    }
}

/// Helper function used to preallocate and possibly initialize a BouncedDiskFile.
fn preallocate_bounced_disk_file<P: AsRef<Path>>(
    path: P,
    size: i64,
    zero_out: bool,
) -> Result<BouncedDiskFile> {
    let mut fs_file = preallocate_file(path, size)?;
    if zero_out {
        let mut disk_file = DiskFile::new(&mut fs_file, None)?;
        zero_disk_file(&mut disk_file, size)?;
    }

    BouncedDiskFile::new(&mut fs_file, None)
}

/// Write zeroes to the given DiskFile for a size, then seek back to 0.
fn zero_disk_file(disk_file: &mut DiskFile, mut size: i64) -> Result<()> {
    // MmapBuffers come zeroed.
    let buf = MmapBuffer::new(1024 * 64)?;
    while size != 0 {
        let this_size: usize = (std::cmp::min(size, buf.len() as i64)) as usize;
        disk_file.write_all(&buf.u8_slice()[..this_size])?;
        size -= this_size as i64;
    }

    disk_file.seek(SeekFrom::Start(0))?;
    Ok(())
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
        return Err(HibernateError::FallocateError(
            libchromeos::sys::Error::last(),
        ))
        .context("Failed to preallocate via fallocate");
    }

    Ok(file)
}
