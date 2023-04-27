// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements support for collecting and sending hibernate metrics.

use std::fs;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Cursor;
use std::io::Write;
use std::mem;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::path::PathBuf;
use std::process::Command;
use std::time::Duration;

use anyhow::anyhow;
use anyhow::Context;
use anyhow::Result;
use log::warn;
use serde::Deserialize;
use serde::Serialize;

use crate::files::increment_file_counter;
use crate::files::open_attempts_file;
use crate::files::open_hiber_fails_file;
use crate::files::open_resume_failures_file;
use crate::files::HIBERMETA_DIR;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateStage;
use crate::mmapbuf::MmapBuffer;

/// Define the resume metrics file name.
const RESUME_METRICS_FILE_NAME: &str = "resume_metrics";
/// Define the suspend metrics file name.
const SUSPEND_METRICS_FILE_NAME: &str = "suspend_metrics";

/// Bytes per MB float value.
pub const BYTES_PER_MB_F64: f64 = 1048576.0;
/// Max expected IO size for IO metrics.
pub const MAX_IO_SIZE_KB: isize = 9437000;
/// Size of the metrics buffer, 4k aligned to be compatible with BouncedDiskFile writes.
pub const METRICS_BUFFER_SIZE: usize = 4096;

/// A MetricSample represents a sample point for a Hibernate histogram in UMA.
/// It requires the histogram name, the sample value, the minimum value,
/// the maximum value, and the number of buckets.
#[derive(Serialize, Deserialize)]
pub struct MetricsSample<'a> {
    pub name: &'a str,
    pub value: isize,
    pub min: isize,
    pub max: isize,
    pub buckets: usize,
}

/// Define the hibernate metrics logger.
pub struct MetricsLogger {
    pub file: Option<File>,
    buf: MmapBuffer,
    offset: usize,
}

impl MetricsLogger {
    pub fn new() -> Result<Self> {
        let buf = MmapBuffer::new(METRICS_BUFFER_SIZE)?;
        Ok(Self {
            file: None,
            buf,
            offset: 0,
        })
    }

    /// Log a metric to the MetricsLogger buffer, flush if full.
    pub fn log_metric(&mut self, metric: MetricsSample) {
        let log = match serde_json::to_string(&metric) {
            Ok(s) => s,
            Err(e) => {
                warn!("Failed to make metric string, {}", e);
                return;
            }
        };
        let log_str = format!("{}\n", log);

        let metric_bytes = log_str.as_bytes();
        assert!(metric_bytes.len() < self.buf.len());
        let remaining = self.buf.len() - self.offset;
        let copy_size = std::cmp::min(remaining, metric_bytes.len());
        let end = self.offset + copy_size;
        self.buf.u8_slice_mut()[self.offset..end].copy_from_slice(&metric_bytes[0..copy_size]);
        self.offset += copy_size;
        if self.offset == self.buf.len() {
            if let Err(e) = self.flush() {
                warn!("Failed to flush metrics buf to file {:?}", e);
            }
            let remainder = metric_bytes.len() - copy_size;
            self.buf.u8_slice_mut()[0..remainder].copy_from_slice(&metric_bytes[copy_size..]);
            self.offset = remainder;
        }
    }

    /// Write the MetricsLogger buffer to the MetricsLogger file.
    pub fn flush(&mut self) -> Result<()> {
        let remaining = self.buf.len() - self.offset;
        if remaining > 0 {
            let zero = [0u8; METRICS_BUFFER_SIZE];
            self.buf.u8_slice_mut()[self.offset..].copy_from_slice(&zero[..remaining]);
        }
        self.offset = 0;

        match &mut self.file {
            Some(f) => f
                .write_all(self.buf.u8_slice())
                .context("Failed to write metrics file"),
            None => Err(HibernateError::MetricsSendFailure(
                "No metrics file set.".to_string(),
            ))
            .context("Failed to write to metrics file"),
        }
    }

    pub fn metrics_send_io_sample(&mut self, histogram: &str, io_bytes: u64, duration: Duration) {
        let rate = ((io_bytes as f64) / duration.as_secs_f64()) / BYTES_PER_MB_F64;
        let base_name = "Platform.Hibernate.IO.";
        // Convert the bytes to KiB for more manageable metric values.
        let io_kbytes = io_bytes / 1024;
        let size_metric = MetricsSample {
            name: &format!("{}{}.Size", base_name, histogram),
            value: io_kbytes as isize,
            min: 0,
            max: MAX_IO_SIZE_KB,
            buckets: 50,
        };

        let rate_metric = MetricsSample {
            name: &format!("{}{}.Rate", base_name, histogram),
            value: rate as isize,
            min: 0,
            max: 1024,
            buckets: 50,
        };

        let duration_metric = MetricsSample {
            name: &format!("{}{}.Duration", base_name, histogram),
            value: duration.as_secs() as isize,
            min: 0,
            max: 120,
            buckets: 50,
        };

        self.log_metric(size_metric);
        self.log_metric(rate_metric);
        self.log_metric(duration_metric);
    }

    pub fn metrics_send_duration_sample(
        &mut self,
        histogram: &str,
        duration: Duration,
        max: isize,
    ) {
        let mut num_buckets = 50;
        if max < 50 {
            num_buckets = max + 1;
        }
        let base_name = "Platform.Hibernate.Duration.";
        let duration_metric = MetricsSample {
            name: &format!("{}{}", base_name, histogram),
            value: duration.as_secs() as isize,
            min: 0,
            max,
            buckets: num_buckets as usize,
        };

        self.log_metric(duration_metric);
    }
}

/// Struct with associated functions for creating and opening hibernate
/// metrics files.
pub struct MetricsFile {}

impl MetricsFile {
    /// Create the metrics file with the given path, truncate the file if it
    /// already exists. The file is opened with O_SYNC to make sure data from
    /// writes isn't buffered by the kernel but submitted to storage
    /// immediately.
    pub fn create<P: AsRef<Path>>(path: P) -> Result<File> {
        let opts = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .custom_flags(libc::O_SYNC)
            .clone();

        Self::open_file(path, &opts)
    }

    /// Open an existing metrics file at the given path. The file is opened with
    /// O_SYNC to make sure data from writes isn't buffered by the kernel but
    /// submitted to storage immediately.
    pub fn open<P: AsRef<Path>>(path: P) -> Result<File> {
        Self::open_file(
            path,
            OpenOptions::new()
                .read(true)
                .write(true)
                .custom_flags(libc::O_SYNC),
        )
    }

    /// Get the path of the metrics file for a given hibernate stage.
    pub fn get_path(stage: HibernateStage) -> PathBuf {
        let name = match stage {
            HibernateStage::Suspend => SUSPEND_METRICS_FILE_NAME,
            HibernateStage::Resume => RESUME_METRICS_FILE_NAME,
        };

        Path::new(HIBERMETA_DIR).join(name)
    }

    fn open_file<P: AsRef<Path>>(path: P, open_options: &OpenOptions) -> Result<File> {
        match open_options.open(&path) {
            Ok(f) => Ok(f),
            Err(e) => Err(anyhow!(e).context(format!(
                "Failed to open metrics file '{}'",
                path.as_ref().display()
            ))),
        }
    }
}

/// Send metrics_client sample.
fn metrics_send_sample(sample: &MetricsSample) -> Result<()> {
    let status = Command::new("metrics_client")
        .arg("--")
        .arg(sample.name)
        .arg(sample.value.to_string())
        .arg(sample.min.to_string())
        .arg(sample.max.to_string())
        .arg(sample.buckets.to_string())
        .status()?;
    if !status.success() {
        warn!(
            "Failed to send metric {} {} {} {} {}",
            sample.name,
            sample.value.to_string(),
            sample.min.to_string(),
            sample.max.to_string(),
            sample.buckets.to_string(),
        );
        return Err(HibernateError::MetricsSendFailure(format!(
            "Metrics failed to send with exit code: {:?}",
            status.code()
        )))
        .context("Failed to send metrics");
    }
    Ok(())
}

pub fn log_hibernate_attempt() -> Result<()> {
    let mut f = open_attempts_file()?;
    increment_file_counter(&mut f)
}

pub fn log_hibernate_failure() -> Result<()> {
    let mut f = open_hiber_fails_file()?;
    increment_file_counter(&mut f)
}

pub fn log_resume_failure() -> Result<()> {
    let mut f = open_resume_failures_file()?;
    increment_file_counter(&mut f)
}

fn read_and_send_metrics_file(stage: HibernateStage) -> Result<()> {
    let metrics_file_path = MetricsFile::get_path(stage);

    if !metrics_file_path.exists() {
        return Ok(());
    }

    let mut metrics_file = MetricsFile::open(&metrics_file_path)?;
    let mut reader = BufReader::new(&mut metrics_file);
    let mut buf = Vec::<u8>::new();
    reader.read_until(0, &mut buf)?;

    if buf.is_empty() {
        warn!("Metrics file '{}' is empty", metrics_file_path.display());
        return Ok(());
    }

    // Now split that big buffer into lines.
    let len_without_delim = buf.len() - 1;
    let cursor = Cursor::new(&buf[..len_without_delim]);
    for line in cursor.lines() {
        let line = match line {
            Ok(l) => l,
            Err(e) => {
                warn!("Failed to read metrics line, {}", e);
                continue;
            }
        };

        let sample: MetricsSample = match serde_json::from_str(&line) {
            Ok(s) => s,
            Err(e) => {
                warn!("Failed to make metric string, {}", e);
                continue;
            }
        };

        let _ = metrics_send_sample(&sample);
    }

    // All metrics have been processed, delete the metrics file.
    mem::drop(metrics_file);
    if let Err(e) = fs::remove_file(&metrics_file_path) {
        warn!("Failed to remove {}: {}", metrics_file_path.display(), e);
    }

    Ok(())
}

pub fn read_and_send_metrics() {
    if let Err(e) = read_and_send_metrics_file(HibernateStage::Suspend) {
        warn!("Failed to read suspend metrics, {}", e);
    }
    if let Err(e) = read_and_send_metrics_file(HibernateStage::Resume) {
        warn!("Failed to read resume metrics, {}", e);
    }
}
