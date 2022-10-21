// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements support for collecting and sending hibernate metrics.

use std::io::{BufRead, BufReader, Cursor, Write};
use std::process::Command;
use std::time::Duration;

use anyhow::{Context, Result};
use log::warn;
use serde::{Deserialize, Serialize};

use crate::diskfile::BouncedDiskFile;
use crate::files::{
    increment_file_counter, metrics_file_exists, open_attempts_file, open_hiber_fails_file,
    open_metrics_file, open_resume_failures_file,
};
use crate::hiberutil::HibernateError;
use crate::mmapbuf::MmapBuffer;

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

/// Define the known metrics file types.
pub enum MetricsFile {
    Suspend,
    Resume,
}

/// Define the hibernate metrics logger.
pub struct MetricsLogger {
    pub file: Option<BouncedDiskFile>,
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

    pub fn metrics_send_io_sample(&mut self, histogram: &str, io_bytes: i64, duration: Duration) {
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

fn read_and_send_metrics_file(name: MetricsFile) -> Result<()> {
    if !metrics_file_exists(&name) {
        return Ok(());
    }
    let mut metrics_file = open_metrics_file(name)?;
    let mut reader = BufReader::new(&mut metrics_file);
    let mut buf = Vec::<u8>::new();
    reader.read_until(0, &mut buf)?;

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

    // Overwrite the metrics file with zeros to avoid stale metrics in next iteration.
    metrics_file.rewind()?;
    let zero = [0u8; METRICS_BUFFER_SIZE];
    metrics_file
        .write_all(&zero)
        .context("Failed to zero-out metrics file")
}

pub fn read_and_send_metrics() {
    if let Err(e) = read_and_send_metrics_file(MetricsFile::Suspend) {
        warn!("Failed to read suspend metrics, {}", e);
    }
    if let Err(e) = read_and_send_metrics_file(MetricsFile::Resume) {
        warn!("Failed to read resume metrics, {}", e);
    }
}
