// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Standalone functions for running subprocesses / external utilities.

// Some of this is copied / adapted from implementations in os_install_service.

use log::{debug, info};
use std::fmt;
use std::io;
use std::process::{Command, ExitStatus, Output};

#[derive(Debug)]
pub enum ErrorKind {
    LaunchProcess(io::Error),
    ExitedNonZero(ExitStatus),
    ExitedNonZeroWithOutput(Output),
}

#[derive(Debug)]
pub struct ProcessError {
    command: String,
    kind: ErrorKind,
}

impl fmt::Display for ProcessError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        match &self.kind {
            ErrorKind::LaunchProcess(err) => {
                write!(f, "failed to launch process \"{}\": {}", self.command, err)
            }
            ErrorKind::ExitedNonZero(status) => {
                write!(f, "command \"{}\" failed: {}", self.command, status)
            }
            ErrorKind::ExitedNonZeroWithOutput(output) => write!(
                f,
                "command \"{}\" failed: {}\nstdout={}\nstderr={}",
                self.command,
                output.status,
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr),
            ),
        }
    }
}

impl std::error::Error for ProcessError {}

/// A type for passing to `Command::envs`.
///
/// Using `OsString` rather than `String` reduces the number of conversions between Path(Buf) and
/// String we need to do, since `envs` takes things AsRef<OsStr>. All of our keys should be known
/// at compile time, so we can just hold a reference to a static str for those.
pub type Environment = std::collections::BTreeMap<&'static str, std::ffi::OsString>;

/// Format the command as a string for logging.
///
/// There's no good built-in method for this, so use the debug format
/// with quotes removed. The debug format puts quotes around the
/// program and each argument, e.g. `"cmd" "arg1" "arg2"`. Removing
/// all quotes isn't correct in all cases, but good enough for logging
/// purposes.
fn command_to_string(cmd: &Command) -> String {
    format!("{:?}", cmd).replace('"', "")
}

/// Run a command with our standard logging.
///
/// An error is returned if the process fails to launch, or if it exits non-zero.
pub fn log_and_run_command(mut command: Command) -> Result<(), ProcessError> {
    let cmd_str = command_to_string(&command);
    info!("running command: {}", cmd_str);

    let status = match command.status() {
        Ok(status) => status,
        Err(err) => {
            return Err(ProcessError {
                command: cmd_str,
                kind: ErrorKind::LaunchProcess(err),
            });
        }
    };

    if !status.success() {
        return Err(ProcessError {
            command: cmd_str,
            kind: ErrorKind::ExitedNonZero(status),
        });
    }

    Ok(())
}

/// Run a command and get its stdout as raw bytes.
///
/// An error is returned if the process fails to launch, or if it exits non-zero.
pub fn get_command_output(mut command: Command) -> Result<Vec<u8>, ProcessError> {
    let cmd_str = command_to_string(&command);
    debug!("running command: {}", cmd_str);

    let output = match command.output() {
        Ok(output) => output,
        Err(err) => {
            return Err(ProcessError {
                command: cmd_str,
                kind: ErrorKind::LaunchProcess(err),
            });
        }
    };

    if !output.status.success() {
        return Err(ProcessError {
            command: cmd_str,
            kind: ErrorKind::ExitedNonZeroWithOutput(output),
        });
    }
    Ok(output.stdout)
}

/// Run a command and get its stdout as a String.
///
/// An error is returned if the process fails to launch or exits non-zero, or if the output is not
/// valid utf8.
pub fn get_output_as_string(command: Command) -> anyhow::Result<String> {
    let output = get_command_output(command)?;
    let output = String::from_utf8(output)?;
    Ok(output.trim().to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_command_to_string() {
        let mut cmd = Command::new("myCmd");
        cmd.args(["arg1", "arg2"]);
        assert_eq!(command_to_string(&cmd), "myCmd arg1 arg2");
    }

    // On ARM boards this test fails with an ExitNonZero instead of LaunchProcess.
    // We think this comes from how cros tools emulate ARM on x86.
    #[cfg(target_arch = "x86_64")]
    #[test]
    fn test_log_and_run_command_bad_path() {
        let result = log_and_run_command(Command::new("/this/path/does/not/exist"));
        let err = result.unwrap_err();
        assert!(matches!(err.kind, ErrorKind::LaunchProcess(_)));
    }

    #[test]
    fn test_log_and_run_command_success() {
        let mut command = Command::new("echo");
        command.arg("This output shouldn't be captured");
        assert!(log_and_run_command(command).is_ok());
    }

    #[test]
    fn test_log_and_run_command_exit_nonzero() {
        let result = log_and_run_command(Command::new("false"));
        let err = result.unwrap_err();
        assert!(matches!(err.kind, ErrorKind::ExitedNonZero(_)));
    }

    // On ARM boards this test fails with an ExitNonZero instead of LaunchProcess.
    // We think this comes from how cros tools emulate ARM on x86.
    #[cfg(target_arch = "x86_64")]
    #[test]
    fn test_get_command_output_bad_path() {
        let result = get_command_output(Command::new("/this/path/does/not/exist"));
        let err = result.unwrap_err();
        assert!(matches!(err.kind, ErrorKind::LaunchProcess(_)));
    }

    #[test]
    fn test_get_command_output_success() {
        let mut command = Command::new("echo");
        command.arg("myOutput");
        assert_eq!(get_command_output(command).unwrap(), b"myOutput\n");
    }

    #[test]
    fn test_get_command_output_exit_nonzero() {
        let result = get_command_output(Command::new("false"));
        let err = result.unwrap_err();
        assert!(matches!(err.kind, ErrorKind::ExitedNonZeroWithOutput(_)));
    }

    #[test]
    fn test_get_output_as_string() {
        let expected = String::from("myOutput");
        let mut command = Command::new("echo");
        command.arg(&expected);
        assert_eq!(get_output_as_string(command).unwrap(), expected);
    }
}
