// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! MSR Logger
//!
//! This module logs the MSR_CORE_PERF_LIMIT_REASONS register periodically on Intel platform for
//! debugging CPU thermal throttling issues.

use std::fs::File;
use std::io::Write;
use std::os::unix::fs::FileExt;
use std::time::Duration;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use log::error;
use log::info;

use super::platform;
use crate::feature;

const MSR_LOGGER_FEATURE_NAME: &str = "CrOSLateBootResourcedMsrLogger";

const MSR_CORE_PERF_LIMIT_REASONS_ADDR: u64 = 0x64f;

// The MSR_CORE_PERF_LIMIT_REASONS register is global to the processor, so it's okay to only access
// the core 0 MSR_CORE_PERF_LIMIT_REASONS.
const CPU_0_MSR_PATH: &str = "/dev/cpu/0/msr";

// Software should write to the MSR_CORE_PERF_LIMIT_REASONS log bits to clear them.
fn clear_perf_limit_reasons() -> Result<()> {
    let zeros: [u8; 8] = [0; 8];
    let mut file = File::options()
        .write(true)
        .open(CPU_0_MSR_PATH)
        .context(format!(
            "Failed to open MSR file {} in rw mode",
            CPU_0_MSR_PATH
        ))?;

    let bytes_written = file.write_at(&zeros, MSR_CORE_PERF_LIMIT_REASONS_ADDR)?;
    if bytes_written != 8 {
        bail!("Failed to update MSR MSR_CORE_PERF_LIMIT_REASONS_ADDR with value 0");
    }
    file.flush()?;

    Ok(())
}

// Reads MSR_CORE_PERF_LIMIT_REASONS register.
fn get_perf_limit_reasons() -> Result<u64> {
    let mut read_data = [0u8; 8];
    let file = File::open(CPU_0_MSR_PATH)?;
    let num_bytes_read = file.read_at(&mut read_data, MSR_CORE_PERF_LIMIT_REASONS_ADDR)?;
    if num_bytes_read != 8 {
        bail!("unexpected bytes read: {}", num_bytes_read);
    }

    Ok(u64::from_le_bytes(read_data))
}

// Parses the MSR_CORE_PERF_LIMIT_REASONS number to reason strings.
// The reason strings may contain the following 4 reasons:
//   PROCHOT: PROCHOT# has caused IA frequency clipping.
//   THERMAL: Thermal event has caused IA frequency clipping.
//   PL1: package or platform PL1 has caused IA frequency clipping.
//   PL2: package or platform PL2 or PL3 has caused IA frequency clipping.
fn parse_perf_limit_reasons(reasons: u64) -> Vec<&'static str> {
    const PERF_LIMIT_REASONS_PROCHOT_LOG_MASK: u64 = 0x10000;
    const PERF_LIMIT_REASONS_THERMAL_LOG_MASK: u64 = 0x20000;
    const PERF_LIMIT_REASONS_PL1_LOG_MASK: u64 = 0x4000000;
    const PERF_LIMIT_REASONS_PL2_LOG_MASK: u64 = 0x8000000;
    const PERF_LIMIT_REASONS_WATCH_MASK: u64 = PERF_LIMIT_REASONS_PROCHOT_LOG_MASK
        | PERF_LIMIT_REASONS_THERMAL_LOG_MASK
        | PERF_LIMIT_REASONS_PL1_LOG_MASK
        | PERF_LIMIT_REASONS_PL2_LOG_MASK;

    let mut result: Vec<&str> = Vec::new();
    if reasons & PERF_LIMIT_REASONS_WATCH_MASK == 0 {
        return result;
    }
    if reasons & PERF_LIMIT_REASONS_PROCHOT_LOG_MASK != 0 {
        result.push("PROCHOT");
    }
    if reasons & PERF_LIMIT_REASONS_THERMAL_LOG_MASK != 0 {
        result.push("THERMAL");
    }
    if reasons & PERF_LIMIT_REASONS_PL1_LOG_MASK != 0 {
        result.push("PL1");
    }
    if reasons & PERF_LIMIT_REASONS_PL2_LOG_MASK != 0 {
        result.push("PL2");
    }

    result
}

// Logs the perf limit reasons to syslog. Example log:
//   perf limit reasons: PROCHOT, THERMAL, PL1
async fn log_perf_limit_reasons() -> Result<()> {
    let reasons = parse_perf_limit_reasons(get_perf_limit_reasons()?);
    if !reasons.is_empty() {
        info!("perf limit reasons: {}", reasons.join(", "));
        clear_perf_limit_reasons()?;
    }

    Ok(())
}

// Checks the feature flag every 1 hour. When the feature is on, check msr values every 1 minute.
async fn msr_logger_main() -> Result<()> {
    const BATCH_LOG_COUNT: u32 = 60;
    const WAIT_BETWEEN_LOG: Duration = Duration::from_secs(60);

    loop {
        if feature::is_feature_enabled(MSR_LOGGER_FEATURE_NAME)? {
            for _ in 0..BATCH_LOG_COUNT {
                log_perf_limit_reasons().await?;
                tokio::time::sleep(WAIT_BETWEEN_LOG).await;
            }
        } else {
            tokio::time::sleep(BATCH_LOG_COUNT * WAIT_BETWEEN_LOG).await;
        }
    }
}

// Spawns a thread for msr_logger_main on Intel platform.
pub fn init() {
    if !platform::is_intel_platform().unwrap_or(false) {
        return;
    }

    // Replace false with true for local test.
    feature::register_feature(MSR_LOGGER_FEATURE_NAME, true, None);

    tokio::spawn(async move {
        if let Err(e) = msr_logger_main().await {
            error!("msr logger main failed: {}", e);
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_perf_limit_reasons() {
        assert_eq!(parse_perf_limit_reasons(0xFFFF), Vec::<&'static str>::new());
        assert_eq!(parse_perf_limit_reasons(0x10000), vec!["PROCHOT"]);
        assert_eq!(parse_perf_limit_reasons(0x2FFFF), vec!["THERMAL"]);
        assert_eq!(parse_perf_limit_reasons(0x4000000), vec!["PL1"]);
        assert_eq!(parse_perf_limit_reasons(0x800FFFF), vec!["PL2"]);
        assert_eq!(
            parse_perf_limit_reasons(0x30000),
            vec!["PROCHOT", "THERMAL"]
        );
        assert_eq!(
            parse_perf_limit_reasons(0xC03FFFF),
            vec!["PROCHOT", "THERMAL", "PL1", "PL2"]
        );
    }
}
