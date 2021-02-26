// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io;
use std::process::{Command, Output};

use log::debug;

#[derive(Debug)]
pub enum ErrorKind {
    LaunchProcess(io::Error),
    ExitedNonZero(Output),
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
            ErrorKind::ExitedNonZero(output) => write!(
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

/// Run a command and get its stdout as raw bytes. An error is
/// returned if the process fails to launch, or if it exits non-zero.
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
            kind: ErrorKind::ExitedNonZero(output),
        });
    }
    Ok(output.stdout)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_command_to_string() {
        let mut cmd = Command::new("myCmd");
        cmd.args(&["arg1", "arg2"]);
        assert_eq!(command_to_string(&cmd), "myCmd arg1 arg2");
    }

    #[test]
    fn test_get_command_output_bad_path() {
        let result = get_command_output(Command::new("/this/path/does/not/exist"));
        if let Err(err) = result {
            if matches!(err.kind, ErrorKind::LaunchProcess(_)) {
                return;
            }
        }
        panic!("get_command_output did not return a LaunchProcess error");
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
        if let Err(err) = result {
            if matches!(err.kind, ErrorKind::ExitedNonZero(_)) {
                return;
            }
        }
        panic!("get_command_output did not return ExitedNonZero");
    }
}
