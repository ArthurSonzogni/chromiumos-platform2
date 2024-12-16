// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::process_util::{Environment, RunCommand, RunCommandImpl};
use anyhow::Result;
use std::process::Command;

// Env var names. Must match chromeos-install.sh.
const BUSYBOX_DD_FOUND: &str = "BUSYBOX_DD_FOUND";
const LOSETUP_PATH: &str = "LOSETUP_PATH";

/// Get environment variables related to what tools are available.
///
/// In some environments (MiniOS and Flexor), we use tools from busybox,
/// which differ slightly from the tools installed in a normal ChromeOS
/// environment.
///
/// On success, returns an `Environment` with these vars:
/// * BUSYBOX_DD_FOUND: "true" if `dd` is provided by busybox, "false"
///   otherwise.
/// * LOSETUP_PATH: "/bin/losetup" if `losetup` is provided by busybox,
///   "losetup" otherwise.
pub fn get_tool_env() -> Result<Environment> {
    get_tool_env_impl(&RunCommandImpl)
}

/// Check if the `dd` command comes from busybox.
fn is_dd_busybox(run_command: &dyn RunCommand) -> Result<bool> {
    let mut cmd = Command::new("dd");
    cmd.arg("--version");
    let stdout = run_command.get_output_as_string(cmd)?;
    Ok(stdout.contains("BusyBox"))
}

/// Check if the `losetup` command comes from busybox.
fn is_losetup_busybox(run_command: &dyn RunCommand) -> Result<bool> {
    let mut cmd = Command::new("losetup");
    cmd.arg("--version");
    let stdout = run_command.get_output_as_string(cmd)?;
    Ok(stdout.contains("BusyBox"))
}

/// See `get_tool_env` for details.
fn get_tool_env_impl(run_command: &dyn RunCommand) -> Result<Environment> {
    let mut env = Environment::new();

    let is_dd_busybox = is_dd_busybox(run_command)?;
    let is_losetup_busybox = is_losetup_busybox(run_command)?;

    env.insert(
        BUSYBOX_DD_FOUND,
        (if is_dd_busybox { "true" } else { "false" }).into(),
    );
    env.insert(
        LOSETUP_PATH,
        (if is_losetup_busybox {
            "/bin/losetup"
        } else {
            "losetup"
        })
        .into(),
    );

    Ok(env)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::process_util::MockRunCommand;
    use anyhow::anyhow;

    const BUSYBOX_HELP: &str = "BusyBox v1.36.1 (2024-02-04 23:31:36 PST) multi-call binary.";

    /// Test that `get_tool_env_impl` produces the expected vars for a
    /// busybox environment.
    #[test]
    fn test_busybox_env() {
        let mut run_command = MockRunCommand::new();
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok(BUSYBOX_HELP.to_owned()));
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Ok(BUSYBOX_HELP.to_owned()));

        assert_eq!(
            get_tool_env_impl(&run_command).unwrap().into_vec(),
            [
                (BUSYBOX_DD_FOUND, "true".into()),
                (LOSETUP_PATH, "/bin/losetup".into())
            ]
        );
    }

    /// Test that `get_tool_env_impl` produces the expected vars for a
    /// non-busybox environment.
    #[test]
    fn test_non_busybox_env() {
        let mut run_command = MockRunCommand::new();
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok("dd (coreutils) 8.32".to_owned()));
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Ok("losetup from util-linux 2.38.1".to_owned()));

        assert_eq!(
            get_tool_env_impl(&run_command).unwrap().into_vec(),
            [
                (BUSYBOX_DD_FOUND, "false".into()),
                (LOSETUP_PATH, "losetup".into())
            ]
        );
    }

    /// Test that `get_tool_env_impl` propagates errors if `dd` can't be
    /// run.
    #[test]
    fn test_bad_dd() {
        let mut run_command = MockRunCommand::new();
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Err(anyhow!("dd not found")));

        assert!(get_tool_env_impl(&run_command).is_err());
    }

    /// Test that `get_tool_env_impl` propagates errors if `losetup`
    /// can't be run.
    #[test]
    fn test_bad_losetup() {
        let mut run_command = MockRunCommand::new();
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok("dd (coreutils) 8.32".to_owned()));
        run_command
            .expect_get_output_as_string()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Err(anyhow!("losetup not found")));

        assert!(get_tool_env_impl(&run_command).is_err());
    }
}
