// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::time;
use std::path::Path;
use std::thread;

use log::info;

use crate::context::Context;
use crate::cr50::cr50_get_name;
use crate::cr50::gsctool_cmd_successful;
use crate::cr50::run_gsctool_cmd;
use crate::cr50::GSCTOOL_CMD_NAME;
use crate::error::HwsecError;

// TODO (b/246863926)
pub fn cr50_update(ctx: &mut impl Context) -> Result<(), HwsecError> {
    const MAX_RETRIES: u32 = 3;
    const SLEEP_DURATION: u64 = 70;

    info!("Starting");

    let options: Vec<&str>;
    if gsctool_cmd_successful(ctx, vec!["-f", "-s"]) {
        info!("Will use /dev/tpm0");
        options = vec!["-s"];
    } else if gsctool_cmd_successful(ctx, vec!["-f", "-t"]) {
        info!("Will use trunks_send");
        options = vec!["-t"];
    } else {
        info!("Could not communicate with Cr50");
        return Err(HwsecError::GsctoolError(1));
    }

    let cr50_image = cr50_get_name(ctx, &options[..])?;

    if !Path::new(&cr50_image).exists() {
        info!("{} not found, quitting.", cr50_image);
        return Err(HwsecError::GsctoolError(1));
    }

    let mut exit_status: i32 = 0;
    for retries in 0..MAX_RETRIES {
        if retries != 0 {
            thread::sleep(time::Duration::from_secs(SLEEP_DURATION));
        }

        let exe_result = run_gsctool_cmd(ctx, [&options[..], &["-u", &cr50_image]].concat())?;
        exit_status = exe_result.status.code().unwrap();
        if exit_status <= 2 {
            info!("success ({})", exit_status);
            exit_status = 0;
            break;
        }

        info!(
            "{} (options: {:?}) attempt {} error {}",
            GSCTOOL_CMD_NAME,
            options,
            retries + 1,
            exit_status
        );

        let output = format!(
            "{}{}",
            std::str::from_utf8(&exe_result.stdout)
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?,
            std::str::from_utf8(&exe_result.stderr)
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?
        );

        for line in output.lines() {
            info!("{}", line);
        }
    }
    if exit_status == 0 {
        Ok(())
    } else {
        Err(HwsecError::GsctoolError(exit_status))
    }
}
