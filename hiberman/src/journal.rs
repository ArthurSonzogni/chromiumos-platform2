// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::path::PathBuf;
use std::time::Duration;

use log::warn;

use anyhow::Context;
use anyhow::Result;

use crate::files::HIBERMETA_DIR;
use crate::hiberlog::redirect_log;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::HibernateStage;
use crate::volume::ActiveMount;

/// Define the name of the resume log file.
const RESUME_LOG_FILE_NAME: &str = "resume_log";
/// Define the name of the suspend log file.
const SUSPEND_LOG_FILE_NAME: &str = "suspend_log";

/// Struct with associated functions for creating and opening hibernate
/// log files.
///
/// The structure will redirect the hibernate logs to a buffer in memory
/// when the struct goes out of scope.
///
/// The struct is used during the suspend and resume process to ensure
/// that an open log file is always closed before unmounting 'hibermeta'
/// (which hosts the log file).
pub struct LogFile<'m>(pub &'m ActiveMount);

impl<'m> LogFile<'m> {
    /// Divert the log to a file. If the log was previously pointing to syslog
    /// those messages are flushed.
    pub fn new(stage: HibernateStage, create: bool, am: &'m ActiveMount) -> Result<Self> {
        let log_file = if create {
            Self::create(stage)
        } else {
            Self::open(stage)
        }?;
        redirect_log(HiberlogOut::File(Box::new(log_file)));
        Ok(LogFile(am))
    }

    /// Create the log file at given hibernate stage, truncate the file if it already
    /// exists. The file is opened with O_SYNC to make sure data from writes
    /// isn't buffered by the kernel but submitted to storage immediately.
    fn create(stage: HibernateStage) -> Result<File> {
        let p = Self::get_path(stage);

        OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .custom_flags(libc::O_SYNC)
            .open(p.clone())
            .context(format!("Failed to create log file '{}'", p.display()))
    }

    /// Open existing log file at given hibernation stage. The file is opened with
    /// O_SYNC to make sure data from writes isn't buffered by the kernel but
    /// submitted to storage immediately.
    pub fn open(stage: HibernateStage) -> Result<File> {
        let p = Self::get_path(stage);

        OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(libc::O_SYNC)
            .open(p.clone())
            .context(format!("Failed to open log file '{}'", p.display()))
    }

    /// Check if log file exists for a given hibernation stage.
    pub fn exists(stage: HibernateStage) -> bool {
        Self::get_path(stage).exists()
    }

    /// Clear the log file for a given hibernation stage.
    pub fn clear(stage: HibernateStage) {
        let path = Self::get_path(stage);
        if let Err(e) = fs::remove_file(&path) {
            warn!("Failed to remove {}: {}", path.display(), e);
        }
    }

    /// Get the path of the log file for a given hibernate stage.
    fn get_path(stage: HibernateStage) -> PathBuf {
        let name = match stage {
            HibernateStage::Suspend => SUSPEND_LOG_FILE_NAME,
            HibernateStage::Resume => RESUME_LOG_FILE_NAME,
        };

        Path::new(HIBERMETA_DIR).join(name)
    }
}

impl Drop for LogFile<'_> {
    fn drop(&mut self) {
        redirect_log(HiberlogOut::BufferInMemory);
    }
}

/// Provides an API for recording and reading timestamps from disk.
#[allow(dead_code)]
pub struct TimestampFile<'a> {
    am: &'a ActiveMount,
}

impl<'a> TimestampFile<'a> {
    /// Create new timestamp file record.
    pub fn new(am: &'a ActiveMount) -> Self {
        TimestampFile { am }
    }

    /// Record a timestamp to a file.
    pub fn record_timestamp(&self, name: &str, timestamp: &Duration) -> Result<()> {
        let path = Self::full_path(name);

        let mut f = File::options()
            .create(true)
            .truncate(true)
            .write(true)
            .open(&path)?;

        f.write_all(timestamp.as_millis().to_string().as_bytes())
            .context(format!("Failed to write timestamp to {}", path.display()))
    }

    /// Read a timestamp from a file.
    pub fn read_timestamp(&self, name: &str) -> Result<Duration> {
        let path = Self::full_path(name);
        let ts = fs::read_to_string(&path)
            .context(format!("Failed to read timestamp from {}", path.display()))?;
        let millis =
            ts.parse()
                .context(format!("invalid timestamp in {}: {}", path.display(), ts))?;

        Ok(Duration::from_millis(millis))
    }

    fn full_path(name: &str) -> PathBuf {
        PathBuf::from(format!("/{HIBERMETA_DIR}/{name}"))
    }
}
