// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::OpenOptions;
use std::io::Write;
use std::path::Path;
use std::process::{Command, ExitStatus, Stdio};

use crate::EncodeTestError;

/// Write to a log file in append mode
fn write_to_log(output: &str, log_path: Option<&Path>) {
    if let Some(path) = log_path {
        if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(path) {
            if let Err(err) = file.write_all(output.as_bytes()) {
                log::error!("Failed to append stdout to {:?}: {}", path, err);
            }
        }
    }
}

/// Executes an external command and optionally logs its stdout and stderr to files.
pub fn execute(
    test_binary_path: &Path,
    args: &[&str],
    stdout_path: Option<&Path>,
    stderr_path: Option<&Path>,
) -> Result<ExitStatus, EncodeTestError> {
    let mut command = Command::new(test_binary_path);
    command.args(args);

    command.stdout(Stdio::piped()).stderr(Stdio::piped());
    let output_result = command.output();
    let command_str = format!("{} {}", test_binary_path.display(), args.join(" "));
    log::info!("Executing command: {}", command_str);

    match output_result {
        Ok(output) => {
            let status = output.status;
            let stdout_output = String::from_utf8_lossy(&output.stdout);
            write_to_log(&stdout_output, stdout_path);
            let stderr_output = String::from_utf8_lossy(&output.stderr);
            write_to_log(&stderr_output, stderr_path);
            if status.success() {
                log::info!("Command succeeded: {}", command_str);
                Ok(status)
            } else {
                log::error!("Command: {} failed with error: {:?}", command_str, stderr_output);
                Err(EncodeTestError::CommandExecutionError {
                    command: command_str,
                    stderr: format!("{}", stderr_output),
                })
            }
        }
        Err(err) => Err(EncodeTestError::IoError(format!(
            "Error executing command: {} with error: {:?}",
            command_str, err
        ))),
    }
}
