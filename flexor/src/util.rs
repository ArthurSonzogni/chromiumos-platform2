// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use log::{error, info};
use std::{process::Command, str::from_utf8};

// Executes a command and logs its result. There are three outcomes when
// executing a command:
// 1. Everything is fine, executing the command returns exit code zero.
// 2. The command is not found and can thus not be executed.
// 3. The command is found and executed, but returns a non-zero exit code.
// The returned [`Result`] from this function maps 1. to `Ok` and 2., 3.
// To the `Err`` case.
pub fn execute_command(mut command: Command) -> Result<()> {
    info!("Executing command: {:?}", command);

    match command.status() {
        Ok(status) if status.success() => {
            info!("Executed command succesfully; omitting logs.");
            Ok(())
        }
        Err(err) => {
            error!("Executed command failed: {err}");
            Err(anyhow!("Unable to execute command: {err}"))
        }
        Ok(status) => {
            let status_code = status.code().unwrap_or(-1);
            error!("Executed command failed:  Got error status code: {status_code}",);

            let output = command.output().context("Unable to collect logs.")?;
            let stdout = from_utf8(&output.stdout).context("Unable to collect logs.")?;
            let stderr = from_utf8(&output.stderr).context("Unable to collect logs.")?;

            error!(
                "Logs of the failing command: {}",
                &format!("stdout: {}; stderr: {}", stdout, stderr,)
            );
            Err(anyhow!("Got bad status code: {status_code}"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_execute_bad_commands() {
        // This fails even before executing the command because it doesn't exist.
        let result = execute_command(Command::new("/this/does/not/exist"));
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.to_string().contains("Unable to execute"));

        // This fails due to a bad status code of the command.
        let result = execute_command(Command::new("false"));
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.to_string().contains("Got bad status code"));
    }

    #[test]
    fn test_execute_good_command() {
        let result = execute_command(Command::new("ls"));
        assert!(result.is_ok());
    }
}
