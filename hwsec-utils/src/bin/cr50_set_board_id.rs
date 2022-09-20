// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::process::exit;

use hwsec_utils::command_runner::CommandRunner;
use hwsec_utils::context::Context;
use hwsec_utils::context::RealContext;
use hwsec_utils::cr50::check_cr50_support;
use hwsec_utils::cr50::check_device;
use hwsec_utils::cr50::cr50_check_board_id_and_flag;
use hwsec_utils::cr50::cr50_set_board_id_and_flag;
use hwsec_utils::cr50::generic_tpm2_set_board_id;
use hwsec_utils::cr50::Cr50SetBoardIDVerdict;
use hwsec_utils::cr50::Version;
use hwsec_utils::cr50::GSC_NAME;
use hwsec_utils::cr50::PLATFORM_INDEX;
use log::error;

fn exit_if_not_support_partial_board_id(ctx: &mut impl Context) {
    check_cr50_support(
        ctx,
        Version {
            epoch: 0,
            major: 3,
            minor: 24,
        },
        Version {
            epoch: 0,
            major: 4,
            minor: 24,
        },
        "partial board id",
    )
    .map_err(|e| exit(e as i32))
    .unwrap();
}
fn main() {
    let mut real_ctx = RealContext::new();
    let args_string: Vec<String> = env::args().collect();
    let args: Vec<&str> = args_string.iter().map(|s| s.as_str()).collect();
    if args.len() <= 1 || args.len() >= 4 {
        error!("Usage: {} phase [board_id]", args[0]);
        exit(Cr50SetBoardIDVerdict::GeneralError as i32)
    }
    let phase: &str = args[1];
    if phase == "check_device" {
        check_device(&mut real_ctx)
            .map_err(|e| exit(e as i32))
            .unwrap();
    }
    let mut rlz: &str = if args.len() == 3 { args[2] } else { "" };
    let flag: u16 = if phase == "whitelabel_pvt_flags" {
        if GSC_NAME == "cr50" {
            exit_if_not_support_partial_board_id(&mut real_ctx);
        }
        rlz = "0xffffffff";
        0x3f80
    } else if phase == "whitelabel_dev_flags" {
        if GSC_NAME == "cr50" {
            exit_if_not_support_partial_board_id(&mut real_ctx);
        }
        rlz = "0xffffffff";
        0x3f7f
    } else if phase == "whitelabel_pvt" {
        0x3f80
    } else if phase == "whitelabel_dev" {
        0x3f7f
    } else if phase == "unknown" {
        0xff00
    } else if phase == "dev"
        || phase.starts_with("proto")
        || phase.starts_with("evt")
        || phase.starts_with("dvt")
    {
        0x7f7f
    } else if phase.starts_with("mp") || phase.starts_with("pvt") {
        0x7f80
    } else {
        error!("Unknown phase ({})", phase);
        exit(Cr50SetBoardIDVerdict::GeneralError as i32);
    };

    let tmp_vec: Vec<u8>;
    if rlz.is_empty() {
        if let Ok(cros_config_exec_result) = real_ctx
            .cmd_runner()
            .run("cros_config", vec!["/", "brand-code"])
        {
            if cros_config_exec_result.status.success() {
                tmp_vec = cros_config_exec_result.stdout;
                rlz = std::str::from_utf8(&tmp_vec).unwrap();
            } else {
                error!("cros_config returned non-zero.");
                exit(Cr50SetBoardIDVerdict::GeneralError as i32);
            }
        } else {
            error!("Failed to run cros_config.");
            exit(Cr50SetBoardIDVerdict::GeneralError as i32);
        }
    }

    match rlz.len() {
        0 => {
            error!("No RLZ brand code assigned yet.");
            exit(Cr50SetBoardIDVerdict::GeneralError as i32);
        }
        4 => {
            // Valid RLZ consists of 4 letters
        }
        10 => {
            if rlz != "0xffffffff" {
                error!("Only support erased hex RLZ not {}", rlz);
                exit(Cr50SetBoardIDVerdict::GeneralError as i32);
            }
        }
        _ => {
            error!("Invalid RLZ brand code ({}).", rlz);
            exit(Cr50SetBoardIDVerdict::GeneralError as i32);
        }
    };

    let hooray_message: String = format!(
        r"Successfully updated board ID to '{}' with '{}'.",
        rlz, phase
    );

    let rlz: u32 = if rlz.len() == 10 {
        0xffffffff
    } else {
        u32::from_be_bytes(rlz.as_bytes().try_into().unwrap())
    };

    cr50_check_board_id_and_flag(&mut real_ctx, rlz, flag)
        .map_err(|e| exit(e as i32))
        .unwrap();

    if PLATFORM_INDEX {
        generic_tpm2_set_board_id(&mut real_ctx, rlz, flag)
            .map_err(|e| exit(e as i32))
            .unwrap();
    } else {
        cr50_set_board_id_and_flag(&mut real_ctx, rlz, flag)
            .map_err(|e| exit(e as i32))
            .unwrap();
    }

    println!("{}", hooray_message);
}
