// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::command_runner::CommandRunner;
use crate::context::Context;
use crate::error::HwsecError;
use crate::tpm2::*;

// Check the field description here.
// Reference: https://trustedcomputinggroup.org/wp-content/uploads/TCG_TPM2_r1p59_Part3_Commands_pub.pdf#page=406
fn gen_tpm_cmd(
    tag: TpmiStCommandTag,
    cmd_arg: CommandArg,
    index: u32,
) -> Result<Vec<u8>, HwsecError> {
    let mut ret = Vec::<u8>::new();
    ret.extend_from_slice(&tag.command_code());
    ret.extend_from_slice(&0x00000000_u32.to_be_bytes()); // size: will be overwritten later
    ret.extend_from_slice(&cmd_arg.command_code());
    ret.extend_from_slice(&index.to_be_bytes());
    ret.extend_from_slice(&index.to_be_bytes());
    let TpmiStCommandTag::TPM_ST_SESSIONS(session_option) = tag;
    ret.extend_from_slice(&session_option.command_code());
    match cmd_arg {
        CommandArg::TPM_CC_NV_Write(data) => {
            if data.len() > u16::MAX as usize {
                return Err(HwsecError::InvalidArgumentError);
            }
            ret.extend_from_slice(&(data.len() as u16).to_be_bytes());
            ret.extend_from_slice(&data);
            // offset, which is always 0x0000 in this scenario
            ret.extend_from_slice(&0x0000_u16.to_be_bytes());
        }
        CommandArg::TPM_CC_NV_WriteLock => {
            // empty cmd_param
        }
        CommandArg::TPM_CC_NV_Read(data_len) => {
            ret.extend_from_slice(&data_len.to_be_bytes());
            // offset, which is always 0x0000 in this scenario
            ret.extend_from_slice(&0x0000_u16.to_be_bytes());
        }
    };

    let cmd_size = (ret.len() as u32).to_be_bytes();
    ret[2..6].clone_from_slice(&cmd_size);
    Ok(ret)
}

fn trunksd_is_running(ctx: &mut impl Context) -> bool {
    if let Ok(o) = ctx.cmd_runner().run("status", vec!["trunksd"]) {
        std::str::from_utf8(&o.stdout).unwrap().contains("running")
    } else {
        false
    }
}

fn run_tpm_cmd(ctx: &mut impl Context, tpm_cmd: Vec<u8>) -> Result<TpmCmdResponse, HwsecError> {
    let trunksd_on: bool = trunksd_is_running(ctx);

    let send_util = if trunksd_on { "trunks_send" } else { "tpmc" };

    let flag: Vec<&str> = if trunksd_on {
        vec!["--raw"]
    } else {
        vec!["raw"]
    };

    let arg = TpmCmdArg::new(tpm_cmd);

    let output = ctx
        .cmd_runner()
        .run(
            send_util,
            [
                &flag[..],
                &arg.to_hex_tokens()
                    .iter()
                    .map(AsRef::as_ref)
                    .collect::<Vec<&str>>()[..],
            ]
            .concat(),
        )
        .map_err(|_| HwsecError::CommandRunnerError)?;
    if output.status.success() {
        Ok(TpmCmdResponse::from_send_util_output(output.stdout)?)
    } else {
        Err(HwsecError::CommandRunnerError)
    }
}

pub fn nv_read(ctx: &mut impl Context, index: u32, length: u16) -> Result<Vec<u8>, HwsecError> {
    const SESSION_DESCRIPTION_LENGTH: usize = 6;
    let tpm_cmd = gen_tpm_cmd(
        TpmiStCommandTag::TPM_ST_SESSIONS(SessionOption::EmptyPassword),
        CommandArg::TPM_CC_NV_Read(length),
        index,
    )?;
    let response = run_tpm_cmd(ctx, tpm_cmd)?;
    if response.success() {
        if response.body().len() < SESSION_DESCRIPTION_LENGTH + length as usize {
            // This only happens when TPM is not functioning correctly
            // by returning less bytes than we requested.
            Err(HwsecError::InternalError)
        } else {
            Ok(response.body()
                [SESSION_DESCRIPTION_LENGTH..(SESSION_DESCRIPTION_LENGTH + length as usize)]
                .to_vec())
        }
    } else {
        Err(HwsecError::Tpm2Error(response.return_code()))
    }
}

pub fn nv_write(ctx: &mut impl Context, index: u32, data: Vec<u8>) -> Result<(), HwsecError> {
    let tpm_cmd = gen_tpm_cmd(
        TpmiStCommandTag::TPM_ST_SESSIONS(SessionOption::EmptyPassword),
        CommandArg::TPM_CC_NV_Write(data),
        index,
    )?;
    let response = run_tpm_cmd(ctx, tpm_cmd)?;
    if response.success() {
        Ok(())
    } else {
        Err(HwsecError::Tpm2Error(response.return_code()))
    }
}

pub fn nv_write_lock(ctx: &mut impl Context, index: u32) -> Result<(), HwsecError> {
    let tpm_cmd = gen_tpm_cmd(
        TpmiStCommandTag::TPM_ST_SESSIONS(SessionOption::EmptyPassword),
        CommandArg::TPM_CC_NV_WriteLock,
        index,
    )?;
    let response = run_tpm_cmd(ctx, tpm_cmd)?;
    if response.success() {
        Ok(())
    } else {
        Err(HwsecError::Tpm2Error(response.return_code()))
    }
}

const BOARD_ID_INDEX: u32 = 0x013fff00_u32;
const BOARD_ID_LENGTH: u16 = 12;
pub fn read_board_id(ctx: &mut impl Context) -> Result<data_types::BoardID, HwsecError> {
    let raw_board_id = nv_read(ctx, BOARD_ID_INDEX, BOARD_ID_LENGTH)?;
    Ok(data_types::BoardID {
        part_1: u32::from_le_bytes(raw_board_id[0..4].try_into().unwrap()),
        part_2: u32::from_le_bytes(raw_board_id[4..8].try_into().unwrap()),
        flag: u32::from_le_bytes(raw_board_id[8..12].try_into().unwrap()),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::context::mock::MockContext;

    fn split_into_hex_strtok(hex_code: &str) -> Vec<&str> {
        // e.g. "12 34 56 78" -> ["12", "34", "56", "78"]
        hex_code.split(' ').collect::<Vec<&str>>()
    }

    #[test]
    fn test_nv_read_successful() {
        let index = BOARD_ID_INDEX;
        let length = BOARD_ID_LENGTH;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0c 00 00",
            ),
            0,
            "800200000021000000000000000E000C4646524DB9B9ADB27F7F00000000010000",
            "",
        );

        let result = nv_read(&mut mock_ctx, index, length);

        assert_eq!(
            result,
            Ok(vec![
                0x46, 0x46, 0x52, 0x4d, 0xb9, 0xb9, 0xad, 0xb2, 0x7f, 0x7f, 0x00, 0x00
            ])
        );
    }
    #[test]
    fn test_nv_read_length_too_large() {
        let index = BOARD_ID_INDEX;
        let length = 13_u16;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0d 00 00",
            ),
            0,
            "80010000000A00000146",
            "",
        );

        let result = nv_read(&mut mock_ctx, index, length);

        // return code 0x00000146 = TPM_RC_NV_RANGE = RC_VER1(0x100) + 0x046
        assert_eq!(result, Err(HwsecError::Tpm2Error(0x00000146)));
    }
    #[test]
    fn test_nv_read_nonzero_exit_status() {
        let index = BOARD_ID_INDEX;
        let length = 13_u16;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0d 00 00",
            ),
            1,
            "",
            "",
        );

        let result = nv_read(&mut mock_ctx, index, length);
        assert_eq!(result, Err(HwsecError::CommandRunnerError));
    }
    #[test]
    fn test_nv_write_successful() {
        let index = BOARD_ID_INDEX;
        let data: Vec<u8> = vec![0xff, 0x01, 0x23];

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 26 00 00 \
                01 37 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                03 ff 01 23 00 00",
            ),
            0,
            "800200000021000000000000000E000C4646524DB9B9ADB27F7F00000000010000",
            "",
        );

        let result = nv_write(&mut mock_ctx, index, data);
        assert_eq!(result, Ok(()));
    }
    #[test]
    fn test_nv_write_bad_formatted_response() {
        let index = 0x89abcdef_u32;
        let data: Vec<u8> = vec![
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
            0xee, 0xff,
        ];

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 33 00 00 \
                01 37 89 ab cd ef 89 ab \
                cd ef 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                10 00 11 22 33 44 55 66 \
                77 88 99 aa bb cc dd ee \
                ff 00 00",
            ),
            0,
            // out[4..12]: 0x0000000B isn't its length, should have been 0x0000000A
            // this inaccurate description of length triggers a bad format error,
            // which is what we want to test here.
            "80010000000B00009487",
            "",
        );

        let result = nv_write(&mut mock_ctx, index, data);
        assert_eq!(result, Err(HwsecError::Tpm2ResponseBadFormatError));
    }
    #[test]
    fn test_nv_write_nonzero_exit_status() {
        let index = 0x89abcdef_u32;
        let data: Vec<u8> = vec![
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
            0xee, 0xff,
        ];

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 33 00 00 \
                01 37 89 ab cd ef 89 ab \
                cd ef 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                10 00 11 22 33 44 55 66 \
                77 88 99 aa bb cc dd ee \
                ff 00 00",
            ),
            1,
            "",
            "",
        );

        let result = nv_write(&mut mock_ctx, index, data);
        assert_eq!(result, Err(HwsecError::CommandRunnerError));
    }
    #[test]
    fn test_nv_write_lock_successful() {
        let index = BOARD_ID_INDEX;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 1f 00 00 \
                01 38 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00",
            ),
            0,
            "800200000021000000000000000E000C4646524DB9B9ADB27F7F00000000010000",
            "",
        );

        let result = nv_write_lock(&mut mock_ctx, index);
        assert_eq!(result, Ok(()));
    }
    #[test]
    fn test_nv_write_lock_unsuccessful_response() {
        let index = 0x76543210_u32;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 1f 00 00 \
                01 38 76 54 32 10 76 54 \
                32 10 00 00 00 09 40 00 \
                00 09 00 00 00 00 00",
            ),
            0,
            "80010000000A13371337",
            "",
        );

        let result = nv_write_lock(&mut mock_ctx, index);
        // 0x13371337 is just a piece of random stuff to test response parsing,
        // not referring to any specific error
        assert_eq!(result, Err(HwsecError::Tpm2Error(0x13371337)));
    }
    #[test]
    fn test_nv_write_lock_nonzero_exit_status() {
        let index = 0x76543210_u32;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 1f 00 00 \
                01 38 76 54 32 10 76 54 \
                32 10 00 00 00 09 40 00 \
                00 09 00 00 00 00 00",
            ),
            1,
            "",
            "",
        );

        let result = nv_write_lock(&mut mock_ctx, index);
        assert_eq!(result, Err(HwsecError::CommandRunnerError));
    }
    #[test]
    fn test_tpmc_raw_successful() {
        let index = BOARD_ID_INDEX;
        let length = 12_u16;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(false);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "tpmc",
            vec!["raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0c 00 00",
            ),
            0,
            "0x80 0x02 0x00 0x00 0x00 0x21 0x00 0x00 \n
            0x00 0x00 0x00 0x00 0x00 0x0e 0x00 0x0c \n
            0x46 0x46 0x52 0x4d 0xb9 0xb9 0xad 0xb2 \n
            0x7f 0x7f 0x00 0x00 0x00 0x00 0x01 0x00 \n
            0x00",
            "",
        );

        let result = nv_read(&mut mock_ctx, index, length);
        assert_eq!(
            result,
            Ok(vec![
                0x46, 0x46, 0x52, 0x4d, 0xb9, 0xb9, 0xad, 0xb2, 0x7f, 0x7f, 0x00, 0x00
            ])
        );
    }
    #[test]
    fn nonzero_tpmc_return_code() {
        let index = BOARD_ID_INDEX;
        let length = 12_u16;

        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(false);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "tpmc",
            vec!["raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0c 00 00",
            ),
            -1,
            "",
            "bad byte value \"8002000000230000014e013\
            fff00013fff0000000009400000090000000000000c0000\"",
        );

        let result = nv_read(&mut mock_ctx, index, length);
        assert_eq!(result, Err(HwsecError::CommandRunnerError));
    }
    #[test]
    fn test_read_board_id_successful() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0c 00 00",
            ),
            0,
            "800200000021000000000000000E000C4646524DB9B9ADB27F7F00000000010000",
            "",
        );

        let result = read_board_id(&mut mock_ctx);
        assert_eq!(
            result,
            Ok(data_types::BoardID {
                part_1: 0x4d524646,
                part_2: 0xb2adb9b9,
                flag: 0x00007f7f
            })
        );
    }
    #[test]
    fn test_read_board_id_nonzero_exit_status() {
        let mut mock_ctx = MockContext::new();

        mock_ctx.cmd_runner().set_trunksd_running(true);
        mock_ctx.cmd_runner().add_tpm_interaction(
            "trunks_send",
            vec!["--raw"],
            split_into_hex_strtok(
                "80 02 00 00 00 23 00 00 \
                01 4e 01 3f ff 00 01 3f \
                ff 00 00 00 00 09 40 00 \
                00 09 00 00 00 00 00 00 \
                0c 00 00",
            ),
            1,
            "",
            "",
        );

        let result = read_board_id(&mut mock_ctx);
        assert_eq!(result, Err(HwsecError::CommandRunnerError));
    }
    #[test]
    fn test_nv_write_data_too_large() {
        let index = BOARD_ID_INDEX;
        let data: Vec<u8> = vec![0x99; 1000000];

        let mut mock_ctx = MockContext::new();

        let result = nv_write(&mut mock_ctx, index, data);
        assert_eq!(result, Err(HwsecError::InvalidArgumentError));
    }
}
