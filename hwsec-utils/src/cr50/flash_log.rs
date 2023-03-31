// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::Path;
use std::str::SplitAsciiWhitespace;
use std::time::SystemTime;

use log::error;
use log::info;

use crate::context::Context;
use crate::cr50::run_gsctool_cmd;
use crate::cr50::run_metrics_client;
use crate::cr50::GSC_METRICS_PREFIX;
use crate::error::HwsecError;

const FE_LOG_NVMEM: u64 = 5;
const NVMEM_MALLOC: u64 = 200;

pub fn read_prev_timestamp_from_file(file_path: &str) -> Result<u64, HwsecError> {
    if !Path::new(&file_path).exists() {
        info!("{} not found, creating.", file_path);
        match fs::write(file_path, b"0") {
            Ok(_) => return Ok(0),
            Err(_) => {
                error!("Failed to create {}", file_path);
                return Err(HwsecError::FileError);
            }
        }
    }

    let Ok(file_string) = fs::read_to_string(file_path) else {
        error!("Failed to read {}", file_path);
        return Err(HwsecError::FileError);
    };

    file_string
        .parse::<u64>()
        .map_err(|_| HwsecError::InternalError)
}

pub fn update_timestamp_file(new_stamp: u64, file_path: &str) -> Result<(), HwsecError> {
    fs::write(file_path, new_stamp.to_string()).map_err(|_| {
        error!("Failed to write timestamp to {}", file_path);
        HwsecError::FileError
    })
}

pub fn set_cr50_log_file_time_base(ctx: &mut impl Context) -> Result<(), HwsecError> {
    let epoch_secs = match SystemTime::now().duration_since(SystemTime::UNIX_EPOCH) {
        Ok(epoch) => epoch.as_secs(),
        Err(_) => return Err(HwsecError::SystemTimeError),
    };

    let gsctool_result = run_gsctool_cmd(ctx, vec!["-a", "-T", &epoch_secs.to_string()])?;
    if !gsctool_result.status.success() {
        error!("Failed to set Cr50 flash log time base to {}", epoch_secs);
        return Err(HwsecError::GsctoolError(
            gsctool_result.status.code().unwrap(),
        ));
    }
    info!("Set Cr50 flash log base time to {}", epoch_secs);
    Ok(())
}

fn get_next_u64_from_iterator(iter: &mut SplitAsciiWhitespace) -> Result<u64, HwsecError> {
    match iter.next() {
        None => {
            error!("Failed to parse gsctool log line");
            Err(HwsecError::InternalError)
        }
        Some(str) => str.parse::<u64>().map_err(|_| {
            error!("Failed to parse gsctool log line");
            HwsecError::InternalError
        }),
    }
}

// The output from "gsctool -a -M -L 0" may look as follows:
//         1:00
// 1623743076:09 00
// 1623743077:09 02
// 1623743085:09 00
// 1623743086:09 01
// 1666170902:09 00
// 1666170905:09 02
fn parse_timestamp_and_event_id_from_log_entry(line: &str) -> Result<(u64, u64), HwsecError> {
    let binding = line.trim().replace(':', " ");
    let mut parts = binding.split_ascii_whitespace();
    let stamp: u64 = get_next_u64_from_iterator(&mut parts)?;
    let mut event_id: u64 = get_next_u64_from_iterator(&mut parts)?;

    if event_id == FE_LOG_NVMEM {
        // If event_id is 05, which is FE_LOG_NVMEM, then adopt '200 + the first
        // byte of payload' as an new event_id, as defined as enum Cr50FlashLogs in
        // https://chromium.googlesource.com/chromium/src/+//main:tools/metrics/
        // histograms/enums.xml.

        // For example, event_id=05, payload[0]=00, then new event id is 200, which
        // is labeled as 'Nvmem Malloc'.
        let payload_0: u64 = get_next_u64_from_iterator(&mut parts)?;
        event_id = NVMEM_MALLOC + payload_0;
    }
    Ok((stamp, event_id))
}

pub fn cr50_flash_log(ctx: &mut impl Context, prev_stamp: u64) -> Result<u64, (HwsecError, u64)> {
    let Ok(gsctool_result) = run_gsctool_cmd(
        ctx,
        vec!["-a", "-M", "-L", &prev_stamp.to_string()]
    ) else {
        return Err((HwsecError::GsctoolError(1), 0))
    };

    if !gsctool_result.status.success() {
        error!("Failed to get flash log entries");
        return Err((
            HwsecError::GsctoolError(gsctool_result.status.code().unwrap()),
            0,
        ));
    }

    let Ok(content) = std::str::from_utf8(&gsctool_result.stdout) else {
        return Err((HwsecError::GsctoolResponseBadFormatError, 0))
    };

    let mut new_stamp: u64 = 0;
    for entry in content.lines() {
        let Ok((stamp, event_id)) = parse_timestamp_and_event_id_from_log_entry(entry) else {
            return Err((HwsecError::InternalError, new_stamp))
        };

        let Ok(metrics_client_result) = run_metrics_client(
            ctx,
            vec![
                "-s",
                &format!("{}.FlashLog", GSC_METRICS_PREFIX),
                &format!("0x{:02x}", event_id),
            ],
        ) else {
            return Err((HwsecError::MetricsClientFailureError, new_stamp))
        };

        if metrics_client_result.status.code().unwrap() == 0 {
            new_stamp = stamp;
        } else {
            error!(
                "Failed to log event {} at timestamp {}",
                event_id, new_stamp
            );
            return Err((HwsecError::InternalError, new_stamp));
        }
    }
    Ok(new_stamp)
}
