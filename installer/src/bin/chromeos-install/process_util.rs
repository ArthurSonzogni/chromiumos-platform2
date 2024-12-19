// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Standalone functions for running subprocesses / external utilities.

// Some of this is copied / adapted from implementations in os_install_service.

use log::{debug, info};
use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fmt::{self, Write};
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
#[derive(Debug)]
pub struct Environment(
    /// Use `OsString` rather than `String` to reduce the number of
    /// conversions between Path(Buf) and String we need to do, since
    /// `envs` takes things AsRef<OsStr>.
    BTreeMap<String, OsString>,
);

impl Environment {
    /// Create an empty environment.
    pub fn new() -> Self {
        Self(BTreeMap::new())
    }

    /// Add key/value pairs from an iterator.
    pub fn extend<K, I>(&mut self, iter: I)
    where
        I: IntoIterator<Item = (K, OsString)>,
        K: Into<String>,
    {
        self.0.extend(iter.into_iter().map(|(k, v)| (k.into(), v)))
    }

    /// Add a single key/value pair.
    pub fn insert<K: Into<String>>(&mut self, key: K, val: OsString) {
        self.0.insert(key.into(), val);
    }

    /// Get an iterator of the key/value pairs. Only used for tests.
    #[cfg(test)]
    pub fn iter(&self) -> impl Iterator<Item = (&String, &OsString)> {
        self.0.iter()
    }

    /// Convert to a `Vec` of key/value pairs. Only used for tests.
    #[cfg(test)]
    pub fn into_vec(self) -> Vec<(String, OsString)> {
        self.0.into_iter().collect()
    }
}

impl IntoIterator for Environment {
    type Item = (String, OsString);
    type IntoIter = std::collections::btree_map::IntoIter<String, OsString>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

/// Format the command as a string for logging and error messages.
///
/// The output includes only the program and args.
///
/// Env vars are not included because because `chromeos-install.sh` is
/// run with a very large number of env vars. Logging all of that on a
/// single line may exceed the rsyslog line length limit, and also makes
/// error messages unnecessarily verbose.
fn command_to_string(cmd: &Command) -> String {
    let mut output = cmd.get_program().to_string_lossy().into_owned();

    for arg in cmd.get_args() {
        // OK to unwrap: writing into a string cannot fail.
        write!(output, " {}", arg.to_string_lossy()).unwrap();
    }

    output
}

/// Format a command's environment variables.
///
/// Each variable is formatted as `NAME="VALUE"`. If there are enough
/// variables that the line length exceeds 80, the output is split into
/// multiple lines. If an individual variable exceeds the length limit,
/// it is placed on its own line.
fn command_env_to_lines(cmd: &Command) -> Vec<String> {
    let soft_line_limit = 80;

    let mut lines: Vec<String> = Vec::new();
    for (k, v) in cmd.get_envs() {
        let s = format!(
            "{}=\"{}\"",
            k.to_string_lossy(),
            v.map(|v| v.to_string_lossy()).unwrap_or_default()
        );

        if let Some(line) = lines.last_mut() {
            if line.len() + s.len() < soft_line_limit {
                // OK to unwrap: writing into a string cannot fail.
                write!(line, " {s}").unwrap();
            } else {
                lines.push(s);
            }
        } else {
            lines.push(s);
        }
    }

    lines
}

/// Run a command with our standard logging.
///
/// An error is returned if the process fails to launch, or if it exits non-zero.
pub fn log_and_run_command(mut command: Command) -> Result<(), ProcessError> {
    let cmd_str = command_to_string(&command);
    info!("running command: {}", cmd_str);
    for line in command_env_to_lines(&command) {
        info!("command env: {}", line);
    }

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

/// Run a command and get its output (both stdout and stderr).
///
/// An error is returned if the process fails to launch, or if it exits non-zero.
fn get_command_output(mut command: Command) -> Result<Output, ProcessError> {
    let cmd_str = command_to_string(&command);
    debug!("running command: {}", cmd_str);
    for line in command_env_to_lines(&command) {
        debug!("command env: {}", line);
    }

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
    Ok(output)
}

/// Run a command and get its stdout as a `String`.
///
/// An error is returned if the process fails to launch or exits non-zero, or if the output is not
/// valid utf8.
pub fn get_output_as_string(command: Command) -> anyhow::Result<String> {
    let output = get_command_output(command)?;
    let output = String::from_utf8(output.stdout)?;
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_command_to_string() {
        let mut cmd = Command::new("myCmd");
        cmd.env("FOO", "BAR");
        cmd.args(["arg1", "arg2"]);
        assert_eq!(command_to_string(&cmd), "myCmd arg1 arg2");
    }

    #[test]
    fn test_command_env_to_lines() {
        let mut cmd = Command::new("myCmd");
        assert!(command_env_to_lines(&cmd).is_empty());

        cmd.env("VAR1", "val1");
        assert_eq!(command_env_to_lines(&cmd), ["VAR1=\"val1\""]);

        cmd.env("VAR2", "val2");
        assert_eq!(command_env_to_lines(&cmd), ["VAR1=\"val1\" VAR2=\"val2\""]);

        cmd.env(
            "VAR3_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG",
            "VALUE",
        );
        assert_eq!(
            command_env_to_lines(&cmd),
            [
                "VAR1=\"val1\" VAR2=\"val2\"",
                "VAR3_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG_LONG=\"VALUE\""
            ]
        );
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
        assert_eq!(
            get_command_output(command).unwrap(),
            Output {
                stdout: b"myOutput\n".to_vec(),
                stderr: Vec::new(),
                status: ExitStatus::default(),
            }
        );
    }

    #[test]
    fn test_get_command_output_exit_nonzero() {
        let result = get_command_output(Command::new("false"));
        let err = result.unwrap_err();
        assert!(matches!(err.kind, ErrorKind::ExitedNonZeroWithOutput(_)));
    }

    #[test]
    fn test_get_output_as_string() {
        let mut command = Command::new("echo");
        command.arg("myOutput");
        assert_eq!(get_output_as_string(command).unwrap(), "myOutput\n");
    }
}
