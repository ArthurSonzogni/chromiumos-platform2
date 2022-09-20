// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::time;
use std::fs;
use std::path::Path;
use std::process::Output;
use std::thread;
use std::time::SystemTime;

use log::error;
use log::info;
use regex::Regex;

use super::RmaSnBits;
use super::Version;
use super::GSCTOOL_CMD_NAME;
use crate::command_runner::CommandRunner;
use crate::context::Context;
use crate::cr50::GSC_IMAGE_BASE_NAME;
use crate::cr50::GSC_METRICS_PREFIX;
use crate::cr50::PLATFORM_INDEX;
use crate::error::HwsecError;
use crate::tpm2::nv_read;
use crate::tpm2::BoardID;
use crate::tpm2::ERASED_BOARD_ID;

pub fn run_gsctool_cmd(ctx: &mut impl Context, options: Vec<&str>) -> Result<Output, HwsecError> {
    #[cfg(feature = "ti50_onboard")]
    let dflag: Vec<&str> = vec!["-D"];
    #[cfg(not(feature = "ti50_onboard"))]
    let dflag: Vec<&str> = Vec::<&str>::new();

    ctx.cmd_runner()
        .run(GSCTOOL_CMD_NAME, [dflag, options].concat())
        .map_err(|_| HwsecError::CommandRunnerError)
}

pub fn get_gsctool_output(
    ctx: &mut impl Context,
    options: Vec<&str>,
) -> Result<String, HwsecError> {
    let gsctool_raw_output = run_gsctool_cmd(ctx, options)?;
    Ok(String::from_utf8_lossy(&gsctool_raw_output.stdout).to_string())
}

pub fn get_gsctool_full_output(
    ctx: &mut impl Context,
    options: Vec<&str>,
) -> Result<String, HwsecError> {
    let gsctool_raw_output = run_gsctool_cmd(ctx, options)?;
    Ok(
        String::from_utf8_lossy(&[gsctool_raw_output.stdout, gsctool_raw_output.stderr].concat())
            .to_string(),
    )
}

fn run_metrics_client(ctx: &mut impl Context, options: Vec<&str>) -> Result<Output, HwsecError> {
    ctx.cmd_runner()
        .run("metrics_client", options)
        .map_err(|_| HwsecError::CommandRunnerError)
}

pub fn gsctool_cmd_successful(ctx: &mut impl Context, options: Vec<&str>) -> bool {
    let output = run_gsctool_cmd(ctx, options);
    output.is_ok() && output.unwrap().status.success()
}

pub fn cr50_read_rma_sn_bits(ctx: &mut impl Context) -> Result<RmaSnBits, HwsecError> {
    const READ_SN_BITS_INDEX: u32 = 0x013fff01;
    const READ_SN_BITS_LENGTH: u16 = 16;
    const READ_STANDALONE_RMA_BYTES_INDEX: u32 = 0x013fff04;
    const READ_STANDALONE_RMA_BYTES_LENGTH: u16 = 4;

    let sn_bits = nv_read(ctx, READ_SN_BITS_INDEX, READ_SN_BITS_LENGTH)?;
    if sn_bits.len() != READ_SN_BITS_LENGTH as usize {
        return Err(HwsecError::InternalError);
    }

    let standalone_rma_sn_bits: Option<[u8; 4]> = if PLATFORM_INDEX {
        let rma_sn_bits = nv_read(
            ctx,
            READ_STANDALONE_RMA_BYTES_INDEX,
            READ_STANDALONE_RMA_BYTES_LENGTH,
        )?;
        if rma_sn_bits.len() != READ_SN_BITS_LENGTH as usize {
            return Err(HwsecError::InternalError);
        }
        Some(
            rma_sn_bits
                .try_into()
                .map_err(|_| HwsecError::InternalError)?,
        )
    } else {
        None
    };

    Ok(RmaSnBits {
        sn_data_version: sn_bits[0..3]
            .try_into()
            .map_err(|_| HwsecError::InternalError)?,
        rma_status: sn_bits[3],
        sn_bits: sn_bits[4..]
            .try_into()
            .map_err(|_| HwsecError::InternalError)?,
        standalone_rma_sn_bits,
    })
}

/// This function finds the first occurrence of a board id representation
/// after the occurrence of the substring "Board ID".
///
/// If raw_response does not contain substring "Board ID"
/// or there is no board id occurring after the position of that of substring "Board ID",
/// this function returns Err(HwsecError::GsctoolResponseBadFormatError).
pub fn extract_board_id_from_gsctool_response(raw_response: &str) -> Result<BoardID, HwsecError> {
    let re: regex::Regex = Regex::new(r"[0-9a-fA-F]{8}:[0-9a-fA-F]{8}:[0-9a-fA-F]{8}").unwrap();
    if let Some(board_id_keyword_pos) = raw_response.find("Board ID") {
        let board_id_str = re
            .find(&raw_response[board_id_keyword_pos..])
            .ok_or(HwsecError::GsctoolResponseBadFormatError)?
            .as_str();
        Ok(BoardID {
            part_1: u32::from_str_radix(&board_id_str[0..8], 16)
                .map_err(|_| HwsecError::InternalError)?,
            part_2: u32::from_str_radix(&board_id_str[9..17], 16)
                .map_err(|_| HwsecError::InternalError)?,
            flag: u32::from_str_radix(&board_id_str[18..26], 16)
                .map_err(|_| HwsecError::InternalError)?,
        })
    } else {
        Err(HwsecError::GsctoolResponseBadFormatError)
    }
}

/// This function finds the first occurrence of a version representation (e.g. 1.3.14)
/// after the occurrence of the specific substring "RW_FW_VER".
///
/// If raw_response does not contain substring "RW_FW_VER"
/// or there is no version representation occurring
/// after the position of that of substring "RW_FW_VER",
/// this function returns Err(HwsecError::GsctoolResponseBadFormatError).
pub fn extract_rw_fw_version_from_gsctool_response(
    raw_response: &str,
) -> Result<Version, HwsecError> {
    let re: regex::Regex = Regex::new(r"([0-9]+\.){2}[0-9]+").unwrap();
    if let Some(keyword_pos) = raw_response.find("RW_FW_VER") {
        let key_str: Vec<&str> = re
            .find(&raw_response[keyword_pos..])
            .ok_or(HwsecError::GsctoolResponseBadFormatError)?
            .as_str()
            .split('.')
            .collect::<Vec<&str>>();
        Ok(Version {
            epoch: key_str[0]
                .parse::<u8>()
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?,
            major: key_str[1]
                .parse::<u8>()
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?,
            minor: key_str[2]
                .parse::<u8>()
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?,
        })
    } else {
        Err(HwsecError::GsctoolResponseBadFormatError)
    }
}

pub fn cr50_get_name(
    ctx: &mut impl Context,
    gsctool_command_options: &[&str],
) -> Result<String, HwsecError> {
    const PRE_PVT_FLAG: u32 = 0x10;

    info!("updater is {}", GSCTOOL_CMD_NAME);

    let exe_result = run_gsctool_cmd(ctx, [gsctool_command_options, &["-i"]].concat())?;
    let exit_status = exe_result.status.code().unwrap();
    let output = format!(
        "{}{}",
        std::str::from_utf8(&exe_result.stdout)
            .map_err(|_| HwsecError::Tpm2ResponseBadFormatError)?,
        std::str::from_utf8(&exe_result.stderr)
            .map_err(|_| HwsecError::Tpm2ResponseBadFormatError)?
    );
    let mut ext = "prod";

    let board_id = extract_board_id_from_gsctool_response(&output)?;

    let board_flags = format!("0x{:02x}", board_id.flag);

    if exit_status != 0 {
        info!("exit status: {}", exit_status);
        info!("output: {}", output);
    } else if board_id == ERASED_BOARD_ID {
        info!("board ID is erased using {} image", ext);
    } else if board_id.flag & PRE_PVT_FLAG != 0 {
        ext = "prepvt";
    }

    info!(
        r"board_id: '{:02x}:{:02x}:{:02x}' board_flags: '{}', extension: '{}'",
        board_id.part_1, board_id.part_2, board_id.flag, board_flags, ext
    );

    return Ok(format!("{}.{}", GSC_IMAGE_BASE_NAME, ext));
}

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

// TODO (b/246978628)
pub fn cr50_flash_log(
    ctx: &mut impl Context,
    mut event_id: u32,
    payload_0: u32,
) -> Result<(), HwsecError> {
    const TIMESTAMP_FILE: &str = "/mnt/stateful_partition/unencrypted/preserve/cr50_flog_timestamp";
    const FE_LOG_NVMEM: u32 = 5;
    const NVMEM_MALLOC: u32 = 200;

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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::context::mock::MockContext;

    fn split_into_hex_strtok(hex_code: &str) -> Vec<&str> {
        // e.g. "12 34 56 78" -> ["12", "34", "56", "78"]
        hex_code.split(' ').collect::<Vec<&str>>()
    }

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_read_rma_sn_bits_success() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 01 01 3f \
                ff 01 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                10 00 00",
            ),
            0,
            "800200000025000000000000001200100FFFFFFF877F50D208EC89E9C1691F540000010000",
            "",
        );

        let rma_sn_bits = cr50_read_rma_sn_bits(&mut mock_ctx);
        assert_eq!(
            rma_sn_bits,
            Ok(RmaSnBits {
                sn_data_version: [0x0f, 0xff, 0xff],
                rma_status: 0xff,
                sn_bits: [0x87, 0x7f, 0x50, 0xd2, 0x08, 0xec, 0x89, 0xe9, 0xc1, 0x69, 0x1f, 0x54],
                standalone_rma_sn_bits: None
            })
        );
    }

    #[test]
    fn test_cr50_read_rma_sn_bits_nv_read_malfunction() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 01 01 3f \
                ff 01 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                10 00 00",
            ),
            1,
            "",
            "",
        );

        let rma_sn_bits = cr50_read_rma_sn_bits(&mut mock_ctx);
        assert_eq!(rma_sn_bits, Err(HwsecError::CommandRunnerError));
    }

    #[cfg(not(feature = "ti50_onboard"))]
    #[test]
    fn test_cr50_get_name_non_ti50() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bcbdaaa6:00007f80\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(
            name,
            Ok(String::from("/opt/google/cr50/firmware/cr50.bin.prod"))
        );
    }

    #[cfg(feature = "ti50_onboard")]
    #[test]
    fn test_cr50_get_name_ti50() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-D", "-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bcbdaaa6:00007f80\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(
            name,
            Ok(String::from("/opt/google/cr50/firmware/ti50.bin.prod"))
        );
    }

    #[cfg(not(feature = "ti50_onboard"))]
    #[test]
    fn test_cr50_get_name_board_id_non_ti50_not_found() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bxbdaaa6:00007f80\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(name, Err(HwsecError::GsctoolResponseBadFormatError));
    }

    #[cfg(feature = "ti50_onboard")]
    #[test]
    fn test_cr50_get_name_board_id_ti50_not_found() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-D", "-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bxbdaaa6:00007f80\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(name, Err(HwsecError::GsctoolResponseBadFormatError));
    }

    #[cfg(not(feature = "ti50_onboard"))]
    #[test]
    fn test_cr50_get_name_non_ti50_different_ext() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bcbdaaa6:00007f10\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(
            name,
            Ok(String::from("/opt/google/cr50/firmware/cr50.bin.prepvt"))
        );
    }

    #[cfg(feature = "ti50_onboard")]
    #[test]
    fn test_cr50_get_name_ti50_different_ext() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-D", "-a", "-i"],
            0,
            "finding_device 18d1:5014\n\
            Found device.\n\
            found interface 3 endpoint 4, chunk_len 64\n\
            READY\n\
            -------\n\
            Board ID space: 43425559:bcbdaaa6:00007f10\n",
            "",
        );

        let name = cr50_get_name(&mut mock_ctx, &["-a"]);
        assert_eq!(
            name,
            Ok(String::from("/opt/google/cr50/firmware/ti50.bin.prepvt"))
        );
    }
}
