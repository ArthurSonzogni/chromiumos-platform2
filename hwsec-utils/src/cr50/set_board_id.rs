// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::fmt::Display;

use log::error;

use super::extract_board_id_from_gsctool_response;
use super::run_gsctool_cmd;
use super::Version;
use super::PLATFORM_INDEX;
use crate::command_runner::CommandRunner;
use crate::context::Context;
use crate::cr50::extract_rw_fw_version_from_gsctool_response;
use crate::tpm2::nv_write;
use crate::tpm2::nv_write_lock;
use crate::tpm2::read_board_id;
use crate::tpm2::ERASED_BOARD_ID;

pub const WHITELABEL: u32 = 0x4000;
pub const VIRTUAL_NV_INDEX_START: u32 = 0x013fff00;

#[derive(Debug, PartialEq)]
pub enum Cr50SetBoardIDVerdict {
    Successful,
    GeneralError,
    AlreadySetError,
    AlreadySetDifferentlyError,
    DeviceStateError,
}

impl Display for Cr50SetBoardIDVerdict {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Cr50SetBoardIDVerdict::Successful => write!(f, "Successful"),
            Cr50SetBoardIDVerdict::GeneralError => write!(f, "GeneralError"),
            Cr50SetBoardIDVerdict::AlreadySetError => write!(f, "AlreadySetError"),
            Cr50SetBoardIDVerdict::AlreadySetDifferentlyError => {
                write!(f, "AlreadySetDifferentlyError")
            }
            Cr50SetBoardIDVerdict::DeviceStateError => write!(f, "DeviceStateError"),
        }
    }
}

impl From<Cr50SetBoardIDVerdict> for i32 {
    fn from(verdict: Cr50SetBoardIDVerdict) -> Self {
        match verdict {
            Cr50SetBoardIDVerdict::Successful => 0,
            Cr50SetBoardIDVerdict::GeneralError => 1,
            Cr50SetBoardIDVerdict::AlreadySetError => 2,
            Cr50SetBoardIDVerdict::AlreadySetDifferentlyError => 3,
            Cr50SetBoardIDVerdict::DeviceStateError => 4,
        }
    }
}

pub fn cr50_check_board_id_and_flag(
    ctx: &mut impl Context,
    new_board_id: u32,
    new_flag: u16,
) -> Result<(), Cr50SetBoardIDVerdict> {
    let board_id_output = if PLATFORM_INDEX {
        read_board_id(ctx)
    } else {
        let gsctool_raw_response = run_gsctool_cmd(ctx, vec!["-a", "-i"]).map_err(|_| {
            error!("Failed to run gsctool.");
            Cr50SetBoardIDVerdict::GeneralError
        })?;
        let board_id_output = std::str::from_utf8(&gsctool_raw_response.stdout).unwrap();
        extract_board_id_from_gsctool_response(board_id_output)
    };
    let board_id = board_id_output.map_err(|e| {
        error!(
            "Failed to execute gsctool or failed to read board id - {}",
            e
        );
        Cr50SetBoardIDVerdict::GeneralError
    })?;

    if board_id.part_1 == ERASED_BOARD_ID.part_1 && board_id.part_2 == ERASED_BOARD_ID.part_2 {
        Ok(())
    } else if board_id.part_1 != new_board_id {
        error!("Board ID had been set differently.");
        Err(Cr50SetBoardIDVerdict::AlreadySetDifferentlyError)
    } else if (board_id.flag ^ (new_flag as u32)) == WHITELABEL {
        error!("Board ID and flag have already been set. Whitelabel mismatched.");
        Err(Cr50SetBoardIDVerdict::AlreadySetError)
    } else if board_id.flag != new_flag as u32 {
        error!("Flag had been set differently.");
        Err(Cr50SetBoardIDVerdict::AlreadySetDifferentlyError)
    } else {
        error!("Board ID and flag have already been set.");
        Err(Cr50SetBoardIDVerdict::AlreadySetError)
    }
}

pub fn cr50_set_board_id_and_flag(
    ctx: &mut impl Context,
    board_id: u32,
    flag: u16,
) -> Result<(), Cr50SetBoardIDVerdict> {
    let updater_arg = &format!("{:08x}:{:08x}", board_id, flag);
    let update_output = run_gsctool_cmd(ctx, vec!["-a", "-i", updater_arg]).map_err(|_| {
        error!("Failed to run gsctool.");
        Cr50SetBoardIDVerdict::GeneralError
    })?;
    if !update_output.status.success() {
        error!("Failed to update with {}.", updater_arg);
        Err(Cr50SetBoardIDVerdict::GeneralError)
    } else {
        Ok(())
    }
}

pub fn generic_tpm2_set_board_id(
    ctx: &mut impl Context,
    part_1: u32,
    flag: u16,
) -> Result<(), Cr50SetBoardIDVerdict> {
    let part_2: u32 = !part_1;
    let board_id: Vec<u8> = [
        part_1.to_le_bytes().to_vec(),
        part_2.to_le_bytes().to_vec(),
        flag.to_le_bytes().to_vec(),
        vec![0x00, 0x00],
    ]
    .concat();

    nv_write(ctx, VIRTUAL_NV_INDEX_START, board_id).map_err(|_| {
        error!("Failed to write board id space.");
        Cr50SetBoardIDVerdict::GeneralError
    })?;

    nv_write_lock(ctx, VIRTUAL_NV_INDEX_START).map_err(|_| {
        error!("Failed to lock board id space.");
        Cr50SetBoardIDVerdict::GeneralError
    })?;
    Ok(())
}

pub fn check_cr50_support(
    ctx: &mut impl Context,
    target_prod: Version,
    target_prepvt: Version,
    desc: &str,
) -> Result<(), Cr50SetBoardIDVerdict> {
    let output = run_gsctool_cmd(ctx, vec!["-a", "-f", "-M"]).map_err(|_| {
        error!("Failed to run gsctool.");
        Cr50SetBoardIDVerdict::GeneralError
    })?;
    if output.status.code().unwrap() != 0 {
        error!("Failed to get the version");
        return Err(Cr50SetBoardIDVerdict::GeneralError);
    }
    let rw_version = extract_rw_fw_version_from_gsctool_response(&format!(
        "{}{}",
        std::str::from_utf8(&output.stdout).map_err(|_| {
            error!("Internal error occurred.");
            Cr50SetBoardIDVerdict::GeneralError
        })?,
        std::str::from_utf8(&output.stderr).map_err(|_| {
            error!("Internal error occurred.");
            Cr50SetBoardIDVerdict::GeneralError
        })?,
    ))
    .map_err(|_| {
        error!("Failed to extract RW_FW_VERSION from gsctool response");
        Cr50SetBoardIDVerdict::GeneralError
    })?;
    let target = if rw_version.is_prod_image() {
        target_prod
    } else {
        target_prepvt
    };
    if rw_version.to_ord() < target.to_ord() {
        error!(
            "Running cr50 {}. {} support was added in .{}.",
            rw_version, desc, target
        );
        Err(Cr50SetBoardIDVerdict::GeneralError)
    } else {
        Ok(())
    }
}

pub fn check_device(ctx: &mut impl Context) -> Result<(), Cr50SetBoardIDVerdict> {
    let flash_output = ctx
        .cmd_runner()
        .run("flashrom", vec!["-p", "host", "--wp-status"])
        .map_err(|_| {
            error!("Failed to run flashrom.");
            Cr50SetBoardIDVerdict::GeneralError
        })?;

    if !flash_output.status.success() {
        error!(
            "{}{}",
            String::from_utf8_lossy(&flash_output.stdout),
            String::from_utf8_lossy(&flash_output.stderr)
        );
        return Err(Cr50SetBoardIDVerdict::DeviceStateError);
    }

    let crossystem_output = ctx
        .cmd_runner()
        .run(
            "crossystem",
            vec![r"'mainfw_type?normal'", r"'cros_debug?0'"],
        )
        .map_err(|_| {
            error!("Failed to run crossystem");
            Cr50SetBoardIDVerdict::GeneralError
        })?;
    if !crossystem_output.status.success() {
        error!("Not running normal image.");
        return Err(Cr50SetBoardIDVerdict::GeneralError);
    }

    let flash_output_string = format!(
        "{}{}",
        String::from_utf8_lossy(&flash_output.stdout),
        String::from_utf8_lossy(&flash_output.stderr)
    );
    if flash_output_string.contains("write protect is disabled") {
        error!("write protection is disabled");
        Err(Cr50SetBoardIDVerdict::DeviceStateError)
    } else {
        Err(Cr50SetBoardIDVerdict::Successful)
    }
}

#[cfg(test)]
mod tests {
    use crate::context::mock::MockContext;
    use crate::context::Context;
    use crate::cr50::cr50_set_board_id_and_flag;
    use crate::cr50::Cr50SetBoardIDVerdict;

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_check_board_id_and_flag_ok() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_check_board_id_and_flag;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "Board ID space: ffffffff:ffffffff:ffffffff",
            "",
        );

        let result = cr50_check_board_id_and_flag(&mut mock_ctx, 0x00000000, 0x0000);
        assert_eq!(result, Ok(()));
    }

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_check_board_id_and_flag_part_1_neq_new_board_id() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_check_board_id_and_flag;
        use crate::cr50::Cr50SetBoardIDVerdict;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "Board ID space: 12345678:23456789:34567890",
            "",
        );

        let result = cr50_check_board_id_and_flag(&mut mock_ctx, 0x1234567a, 0x0000);
        assert_eq!(
            result,
            Err(Cr50SetBoardIDVerdict::AlreadySetDifferentlyError)
        );
    }

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_check_board_id_and_flag_flag_xor_new_flag_eq_whitelabel() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_check_board_id_and_flag;
        use crate::cr50::Cr50SetBoardIDVerdict;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "Board ID space: 12345678:23456789:00000087",
            "",
        );

        let result = cr50_check_board_id_and_flag(&mut mock_ctx, 0x12345678, 0x4087);
        assert_eq!(result, Err(Cr50SetBoardIDVerdict::AlreadySetError));
    }

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_check_board_id_and_flag_board_id_flag_neq_new_flag() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_check_board_id_and_flag;
        use crate::cr50::Cr50SetBoardIDVerdict;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "Board ID space: 12345678:23456789:00001234",
            "",
        );

        let result = cr50_check_board_id_and_flag(&mut mock_ctx, 0x12345678, 0x4087);
        assert_eq!(
            result,
            Err(Cr50SetBoardIDVerdict::AlreadySetDifferentlyError)
        );
    }

    #[cfg(not(feature = "generic_tpm2"))]
    #[test]
    fn test_cr50_check_board_id_and_flag_else_case() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_check_board_id_and_flag;
        use crate::cr50::Cr50SetBoardIDVerdict;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i"],
            0,
            "Board ID space: 12345678:23456789:00001234",
            "",
        );

        let result = cr50_check_board_id_and_flag(&mut mock_ctx, 0x12345678, 0x1234);
        assert_eq!(result, Err(Cr50SetBoardIDVerdict::AlreadySetError));
    }

    #[test]
    fn test_cr50_set_board_id_and_flag_ok() {
        use crate::context::mock::MockContext;
        use crate::context::Context;
        use crate::cr50::cr50_set_board_id_and_flag;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i", "12345678:0000abcd"],
            0,
            "",
            "",
        );

        let result = cr50_set_board_id_and_flag(&mut mock_ctx, 0x12345678, 0xabcd);
        assert_eq!(result, Ok(()));
    }

    #[test]
    fn test_cr50_set_board_id_and_flag_failed() {
        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["-a", "-i", "12345678:0000abcd"],
            1,
            "",
            "",
        );

        let result = cr50_set_board_id_and_flag(&mut mock_ctx, 0x12345678, 0xabcd);
        assert_eq!(result, Err(Cr50SetBoardIDVerdict::GeneralError));
    }

    // TODO (b/249410379): design more unit tests,
    // continue from testing cr50_set_board_id_and_flag
}
