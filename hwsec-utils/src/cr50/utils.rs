// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Write;

use regex::Regex;

use super::Version;
use super::GSCTOOL_CMD_NAME;
use crate::command_runner::CommandRunner;
use crate::context::Context;
use crate::error::HwsecError;
use crate::output::HwsecOutput;
use crate::tpm2::BoardID;

pub fn run_gsctool_cmd(
    ctx: &mut impl Context,
    mut options: Vec<&str>,
) -> Result<HwsecOutput, HwsecError> {
    if cfg!(feature = "ti50_onboard") {
        options.push("--dauntless");
    }

    ctx.cmd_runner()
        .run(GSCTOOL_CMD_NAME, options)
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

pub fn run_metrics_client(
    ctx: &mut impl Context,
    options: Vec<&str>,
) -> Result<HwsecOutput, HwsecError> {
    ctx.cmd_runner()
        .run("metrics_client", options)
        .map_err(|_| HwsecError::CommandRunnerError)
}

pub fn gsctool_cmd_successful(ctx: &mut impl Context, options: Vec<&str>) -> bool {
    let output = run_gsctool_cmd(ctx, options);
    output.is_ok() && output.unwrap().status.success()
}

pub fn u8_slice_to_hex_string(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        write!(&mut s, "{:02x}", b).unwrap();
    }
    s
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

pub fn get_board_id_with_gsctool(ctx: &mut impl Context) -> Result<BoardID, HwsecError> {
    let gsctool_raw_response = run_gsctool_cmd(ctx, vec!["--any", "--board_id"])?;
    let board_id_output = std::str::from_utf8(&gsctool_raw_response.stdout)
        .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?;
    extract_board_id_from_gsctool_response(board_id_output)
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

pub fn clear_terminal() {
    print!("{esc}[2J{esc}[1;1H", esc = 27 as char);
}

pub fn get_gbb_flags(ctx: &mut impl Context) -> Result<u32, HwsecError> {
    let raw_response = ctx
        .cmd_runner()
        .output("futility", vec!["gbb", "--get", "--flash", "--flags"])
        .map_err(|_| HwsecError::CommandRunnerError)?;
    let re: regex::Regex = Regex::new(r"0x[0-9a-fA-F]{8}").unwrap();
    if let Some(keyword_pos) = raw_response.find("flags:") {
        let key_str = re
            .find(&raw_response[keyword_pos..])
            .ok_or(HwsecError::VbootScriptResponseBadFormatError)?
            .as_str();
        Ok(u32::from_str_radix(&key_str[2..], 16)
            .map_err(|_| HwsecError::VbootScriptResponseBadFormatError)?)
    } else {
        Err(HwsecError::VbootScriptResponseBadFormatError)
    }
}

pub fn set_gbb_flags(ctx: &mut impl Context, new_flags: u32) -> Result<(), HwsecError> {
    ctx.cmd_runner()
        .run(
            "futility",
            vec![
                "gbb",
                "--set",
                "--flash",
                &format!("--flags=0x{:08x}", new_flags),
            ],
        )
        .map_err(|_| HwsecError::CommandRunnerError)
        .map(|_| ())
}

pub fn get_hwid(ctx: &mut impl Context) -> Result<String, HwsecError> {
    Ok(ctx
        .cmd_runner()
        .output("crossystem", vec!["hwid"])
        .map_err(|_| HwsecError::CommandRunnerError)?
        .replace(' ', "/"))
}

pub fn get_challenge_string(ctx: &mut impl Context) -> Result<String, HwsecError> {
    // containing whitespace and newline characters
    Ok(get_gsctool_output(ctx, vec!["--trunks_send", "--rma_auth"])
        .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?
        .replace("Challange:", ""))
}

#[cfg(test)]
mod tests {
    use super::extract_rw_fw_version_from_gsctool_response;
    use crate::cr50::Version;
    use crate::error::HwsecError;

    #[test]
    fn test_extract_rw_fw_version_from_gsctool_response_ok() {
        let result = extract_rw_fw_version_from_gsctool_response("RW_FW_VER=0.0.1");
        assert_eq!(
            result,
            Ok(Version {
                epoch: 0,
                major: 0,
                minor: 1
            })
        );
    }

    #[test]
    fn test_extract_rw_fw_version_from_gsctool_response_no_version_after_rw_fw_substring() {
        let result = extract_rw_fw_version_from_gsctool_response("RW_FW_VER=");
        assert_eq!(result, Err(HwsecError::GsctoolResponseBadFormatError));
    }

    #[test]
    fn test_extract_rw_fw_version_from_gsctool_response_no_rw_fw_substring() {
        let result = extract_rw_fw_version_from_gsctool_response("0.0.1");
        assert_eq!(result, Err(HwsecError::GsctoolResponseBadFormatError));
    }

    #[test]
    fn test_extract_rw_fw_version_from_gsctool_response_not_valid_version() {
        let result = extract_rw_fw_version_from_gsctool_response("RW_FW_VER=123");
        assert_eq!(result, Err(HwsecError::GsctoolResponseBadFormatError));
    }
}
