// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::Path;
use std::time::SystemTime;

use log::error;
use log::info;

use crate::context::Context;
use crate::cr50::run_gsctool_cmd;
use crate::cr50::run_metrics_client;
use crate::cr50::GSC_METRICS_PREFIX;
use crate::error::HwsecError;

const TIMESTAMP_FILE: &str = "/mnt/stateful_partition/unencrypted/preserve/cr50_flog_timestamp";
const FE_LOG_NVMEM: u32 = 5;
const NVMEM_MALLOC: u32 = 200;

// TODO (b/246978628)
pub fn cr50_flash_log(
    ctx: &mut impl Context,
    mut event_id: u32,
    payload_0: u32,
) -> Result<(), HwsecError> {
    if !Path::new(&TIMESTAMP_FILE).exists() {
        info!("{} not found, creating.", TIMESTAMP_FILE);
        fs::write(TIMESTAMP_FILE, b"0").map_err(|_| {
            error!("Failed to create {}", TIMESTAMP_FILE);
            HwsecError::FileError
        })?;
    }

    // Set Cr50 flash logger time base
    let epoch_secs = match SystemTime::now().duration_since(SystemTime::UNIX_EPOCH) {
        Ok(epoch) => epoch.as_secs(),
        Err(_) => return Err(HwsecError::SystemTimeError),
    };
    let exit_code = run_gsctool_cmd(ctx, vec!["-a", "-T", epoch_secs.to_string().as_str()])?
        .status
        .code()
        .unwrap();
    if exit_code != 0 {
        info!("Failed to set Cr50 flash log time base to {}", epoch_secs);
        return Err(HwsecError::GsctoolError(1));
    }

    info!("Set Cr50 flash log base time to {}", epoch_secs);

    let file = fs::read_to_string(TIMESTAMP_FILE).map_err(|_| {
        error!("Failed to read {}", TIMESTAMP_FILE);
        HwsecError::FileError
    })?;
    let stamps: Vec<u64> = file.lines().map(|x| x.parse::<u64>().unwrap()).collect();
    if stamps.len() != 1 {
        return Err(HwsecError::InternalError);
    }
    let prev_stamp = stamps[0];

    let gsctool_result =
        run_gsctool_cmd(ctx, vec!["-a", "-M", "-L", prev_stamp.to_string().as_str()])?;

    let output = std::str::from_utf8(&gsctool_result.stdout)
        .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?
        .replace(':', " ");

    for entry in output.lines() {
        let new_stamp = entry.parse::<u64>().map_err(|_| {
            error!("Unexpected content in {}", TIMESTAMP_FILE);
            HwsecError::FileError
        })?;

        if event_id == FE_LOG_NVMEM {
            event_id = NVMEM_MALLOC + payload_0;
        }

        let exit_code = run_metrics_client(
            ctx,
            vec![
                "-s",
                format!("{}.FlashLog", GSC_METRICS_PREFIX).as_str(),
                format!("0x{:02x}", event_id).as_str(),
            ],
        )?
        .status
        .code()
        .unwrap();

        if exit_code == 0 {
            fs::write(TIMESTAMP_FILE, new_stamp.to_string().as_str()).map_err(|_| {
                error!("Failed to create {}", TIMESTAMP_FILE);
                HwsecError::FileError
            })?;
        } else {
            return Err(HwsecError::MetricsClientFailureError(format!(
                "Failed to log event {} at timestamp {}",
                event_id, new_stamp
            )));
        }
    }
    Ok(())
}
