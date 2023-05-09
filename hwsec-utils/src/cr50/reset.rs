// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::time;
use std::fs;
use std::io;
use std::path::Path;
use std::thread;

use log::error;

use super::get_challenge_string;
use super::get_gbb_flags;
use super::get_hwid;
use super::set_gbb_flags;
use crate::command_runner::CommandRunner;
use crate::context::Context;
use crate::cr50::clear_terminal;
use crate::cr50::gsctool_cmd_successful;
use crate::error::HwsecError;

// RMA Reset Authorization parameters.
// - URL of Reset Authorization Server.
const RMA_SERVER: &str = "https://www.google.com/chromeos/partner/console/cr50reset";
// - Number of retries before giving up.
const MAX_RETRIES: i32 = 3;
// - RETRY_DELAY=10
const RETRY_DELAY: i32 = 10;
const FRECON_PID_FILE: &str = "/run/frecon/pid";

fn gbb_force_dev_mode(ctx: &mut impl Context) -> Result<u32, HwsecError> {
    // Disable SW WP and set GBB_FLAG_FORCE_DEV_SWITCH_ON (0x8) to force boot in
    // developer mode after RMA reset.

    // TODO: call flashrom with library instead of commands
    ctx.cmd_runner()
        .run(
            "flashrom",
            vec!["-p", "host", "--wp-disable", "--wp-range", "0,0"],
        )
        .map_err(|_| {
            error!("Failed to run flashrom");
            HwsecError::CommandRunnerError
        })?;

    let flags: u32 = get_gbb_flags(ctx)?;
    let new_flags: u32 = flags | 0x8;
    set_gbb_flags(ctx, new_flags)?;
    Ok(new_flags)
}

pub fn cr50_reset(ctx: &mut impl Context) -> Result<(), HwsecError> {
    const WAIT_TO_ENTER_RMA_SECS: u64 = 2;
    const SECS_IN_A_DAY: u64 = 86400;

    // Make sure frecon is running.
    let frecon_pid = fs::read_to_string(FRECON_PID_FILE).map_err(|_| {
        error!("Failed to read {}", FRECON_PID_FILE);
        HwsecError::FileError
    })?;

    // This is the path to the pre-chroot filesystem. Since frecon is started
    // before the chroot, all files that frecon accesses must be copied to
    // this path.
    let chg_str_path = format!("/proc/{}/root", frecon_pid);

    if !Path::new(&chg_str_path).exists() {
        error!("frecon not running. Can't display qrcode.");
        return Err(HwsecError::FileError);
    }

    // Get HWID and replace whitespace with underscore.
    let hwid = get_hwid(ctx).map_err(|e| {
        error!("Failed to get hwid.");
        e
    })?;

    // Get challenge string and remove "Challenge:".
    let challenge_string = get_challenge_string(ctx).map_err(|e| {
        error!("Failed to get challenge string.");
        e
    })?;

    // Test if we have a challenge.
    if challenge_string.is_empty() {
        error!(r"Challenge wasn't generated. CR50 might need updating.");
        return Err(HwsecError::GsctoolResponseBadFormatError);
    }

    // Preserve enough space to prevent terminal scrolling.
    clear_terminal();

    // Display the challenge.
    println!("Challenge:");
    println!("{}", challenge_string);

    // Remove whitespace and newline from challenge.
    let challenge_string = challenge_string.replace(['\n', ' '], "");

    // Calculate challenge URL and display it.
    let chstr = format!(
        "{}?challenge={}&hwid={}",
        RMA_SERVER, challenge_string, hwid
    );
    println!("\nURL: {}", chstr);

    // Create qrcode and display it.
    // TODO: replace qrencode command with qrcode library like this
    //
    // let qrcode = QrCode::new(challenge_string).unwrap();
    // let image = qrcode.render::<Luma<u8>>().build();
    // image.save(format!("{chg_str_path}/chg.png")).unwrap();
    ctx.cmd_runner()
        .run(
            "qrencode",
            vec![
                "-s",
                "5",
                "-o",
                &format!("{}/chg.png", chg_str_path),
                &chstr,
            ],
        )
        .map_err(|_| {
            error!("Failed to qrencode.");
            HwsecError::QrencodeError
        })?;

    fs::write("/run/frecon/vt0", "\x0033]image:file=/chg.png\x0033\\").map_err(|_| {
        error!("Failed to write to /run/frecon/vt0");
        HwsecError::FileError
    })?;

    for _ in 0..MAX_RETRIES {
        // Read authorization code. Show input in uppercase letters.
        print!("\nEnter authorization code: ");
        let mut auth_code = String::new();
        while io::stdin().read_line(&mut auth_code).is_err() {
            println!("Please only enter ASCII characters.");
        }
        let auth_code = auth_code.to_uppercase();

        // Test authorization code.
        if gsctool_cmd_successful(ctx, vec!["--trunks_send", "--rma_auth", &auth_code]) {
            println!("The system will reboot shortly.");
            // Wait for cr50 to enter RMA mode.
            thread::sleep(time::Duration::from_secs(WAIT_TO_ENTER_RMA_SECS));

            // Force the next boot to be in developer mode so that we can boot to
            // RMA shim again.
            gbb_force_dev_mode(ctx).map_err(|e| {
                error!("gbb_force_dev_mode failed.");
                e
            })?;

            // TODO: reboot with function call instead
            ctx.cmd_runner()
                .run("reboot", Vec::<&str>::new())
                .map_err(|_| {
                    error!("Failed to reboot.");
                    HwsecError::SystemRebootError
                })?;

            // Sleep indefinitely to avoid continue.
            thread::sleep(time::Duration::from_secs(SECS_IN_A_DAY));
        }

        println!("Invalid authorization code. Please try again.\n");
    }

    println!("Number of retries exceeded. Another qrcode will generate in 10s.");

    for _ in 0..RETRY_DELAY {
        print!(".");
        thread::sleep(time::Duration::from_secs(1));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use std::time::SystemTime;
    use std::time::UNIX_EPOCH;

    use crate::command_runner::MockCommandInput;
    use crate::command_runner::MockCommandOutput;
    use crate::context::mock::MockContext;
    use crate::context::Context;
    use crate::cr50::reset::gbb_force_dev_mode;
    use crate::error::HwsecError;

    #[test]
    fn test_gbb_force_dev_mode_successful() {
        const NUM_TEST_CASES: u32 = 100;

        let mut mock_ctx = MockContext::new();
        for _ in 0..NUM_TEST_CASES {
            let old_flag = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .subsec_nanos();
            mock_ctx.cmd_runner().add_expectation(
                MockCommandInput::new(
                    "flashrom",
                    vec!["-p", "host", "--wp-disable", "--wp-range", "0,0"],
                ),
                MockCommandOutput::new(0, "", ""),
            );
            mock_ctx
                .cmd_runner()
                .add_successful_get_gbb_flags_interaction(old_flag);
            mock_ctx
                .cmd_runner()
                .add_successful_set_gbb_flags_interaction(old_flag | 0x8);

            let new_flag = gbb_force_dev_mode(&mut mock_ctx);
            assert_eq!(new_flag, Ok(old_flag | 0x8));
        }
    }
    #[test]
    fn test_gbb_force_dev_mode_failed_to_get_flag() {
        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_expectation(
            MockCommandInput::new(
                "flashrom",
                vec!["-p", "host", "--wp-disable", "--wp-range", "0,0"],
            ),
            MockCommandOutput::new(0, "", ""),
        );
        mock_ctx.cmd_runner().add_expectation(
            MockCommandInput::new("futility", vec!["gbb", "--get", "--flash", "--flags"]),
            MockCommandOutput::new(0, "Oops... no flag ><", ""),
        );

        let new_flag = gbb_force_dev_mode(&mut mock_ctx);
        assert_eq!(new_flag, Err(HwsecError::VbootScriptResponseBadFormatError));
    }

    // TODO (b/249022052): add unit tests for function cr50_reset
}
