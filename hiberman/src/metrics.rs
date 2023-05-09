// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements support for collecting and sending hibernate metrics.

use std::collections::VecDeque;
use std::fs;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Write;
use std::mem;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::path::PathBuf;
use std::process::Command;
use std::sync::Mutex;
use std::time::Duration;

use anyhow::Context;
use anyhow::Result;
use lazy_static::lazy_static;
use log::warn;
use serde::Deserialize;
use serde::Serialize;

use crate::files::increment_file_counter;
use crate::files::open_attempts_file;
use crate::files::open_hiber_fails_file;
use crate::files::open_resume_failures_file;
use crate::files::HIBERMETA_DIR;
use crate::hiberutil::HibernateError;

lazy_static! {
    pub static ref METRICS_LOGGER: Mutex<MetricsLogger> = Mutex::new(MetricsLogger::new());

    /// Path of the file with metric samples.
    static ref METRICS_FILE_PATH: PathBuf = Path::new(HIBERMETA_DIR).join("metrics");
}

/// Bytes per MB float value.
pub const BYTES_PER_MB_F64: f64 = 1048576.0;
/// Max expected IO size for IO metrics.
pub const MAX_IO_SIZE_KB: isize = 9437000;

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
    buf: VecDeque<String>,
}

impl MetricsLogger {
    fn new() -> Self {
        Self {
            buf: VecDeque::new(),
        }
    }

    /// Log a metric to the MetricsLogger buffer.
    pub fn log_metric(&mut self, metric: MetricsSample) {
        let entry = match serde_json::to_string(&metric) {
            Ok(s) => s,
            Err(e) => {
                warn!("Failed to make metric string, {}", e);
                return;
            }
        };

        self.buf.push_back(entry);
    }

    /// Write the MetricsLogger buffer to the MetricsLogger file.
    pub fn flush(&mut self) -> Result<()> {
        if self.buf.is_empty() {
            return Ok(());
        }

        let mut f = File::options()
            .write(true)
            .create(true)
            .append(true)
            .custom_flags(libc::O_SYNC)
            .open(METRICS_FILE_PATH.as_path())
            .context(format!(
                "Failed to open metrics file {}",
                METRICS_FILE_PATH.display()
            ))?;

        for entry in self.buf.drain(..) {
            f.write_all(entry.as_bytes())
                .context("Failed to write metrics file")?;
            f.write_all("\n".as_bytes())
                .context("Failed to write metrics file")?;
        }

        Ok(())
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

pub fn read_and_send_metrics() {
    // Flush any metrics in the buffer to the file before sending the metrics
    let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
    let _ = metrics_logger.flush();

    let res = File::open(METRICS_FILE_PATH.as_path());
    if let Err(e) = res {
        warn!(
            "Failed to open metrics file {}: {}",
            METRICS_FILE_PATH.display(),
            e
        );
        return;
    }

    let mut metrics_file = res.unwrap();
    let reader = BufReader::new(&mut metrics_file);

    for line in reader.lines() {
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
    if let Err(e) = fs::remove_file(METRICS_FILE_PATH.as_path()) {
        warn!("Failed to remove {}: {}", METRICS_FILE_PATH.display(), e);
    }
}
