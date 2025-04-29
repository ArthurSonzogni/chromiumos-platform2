// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module provides helper functions for the ccdec_[codec]_test modules.
// It is kept separate and not exposed in a common file to avoid to having all
// test modules grouped together by test harness.

use std::env::current_exe;
use std::path::Path;
use std::path::PathBuf;
use std::process::{Command, ExitStatus};

const CCDEC_BINARY: &str = "ccdec";
const TEST_DATA_PREFIX: &str = "/data/local/tmp/";

fn get_ccdec_path() -> PathBuf {
    let parent_test_path = current_exe()
        .unwrap()
        .parent()
        .expect("Could not get parent directory of executable")
        .to_path_buf();
    parent_test_path.join(CCDEC_BINARY)
}

fn get_ccdec_args(test_file_path: &Path, json_file_path: &Path, input_format: &str) -> Vec<String> {
    vec![
        test_file_path.display().to_string(),
        "--golden".to_string(),
        json_file_path.display().to_string(),
        "--input-format".to_string(),
        input_format.to_string(),
    ]
}

/// Executes an external command for decoding tests.
fn execute(test_binary_path: &PathBuf, args: &[&str]) -> Result<ExitStatus, String> {
    let mut command = Command::new(test_binary_path);
    command.args(args);
    let output = command.status();

    match output {
        Ok(status) => {
            if status.success() {
                log::info!("Command succeeded: {:?}", command);
                Ok(status)
            } else {
                log::error!("Command failed: {:?} with status: {:?}", command, status);
                Err(format!("Command failed with status: {:?}", status))
            }
        }
        Err(err) => Err(format!("Error executing command: {}", err)),
    }
}

/// Runs cros-codecs integration test for a single file.
pub fn cros_codecs_decode(test_file: &str, input_format: &str) {
    let test_binary_path = get_ccdec_path();
    let test_file_path = Path::new(&test_file);
    assert!(test_file_path.exists(), "{:?} is not a valid path", test_file_path);

    let json_file = format!("{}.json", test_file_path.display());
    let json_file_path = Path::new(&json_file);
    assert!(json_file_path.exists(), "{:?} is not a valid path", json_file_path);

    let ccdec_args = get_ccdec_args(test_file_path, json_file_path, input_format);
    let ccdec_args_str = ccdec_args.iter().map(String::as_str).collect::<Vec<_>>();

    let test_file_name = test_file_path.file_name().unwrap().to_str().unwrap();
    assert!(
        execute(&test_binary_path, &ccdec_args_str).is_ok(),
        "Cros-codecs decode test failed: {}",
        test_file_name
    );
    log::info!("Cros-codecs decode test succeeded: {}", test_file_name);
}

/// Run cros-codecs integration test for a group of test files by codec.
pub fn run_ccdec_test_by_codec_group(test_files: &[&str], input_format: &str) {
    for file in test_files {
        let full_path = format!("{}{}", TEST_DATA_PREFIX, file);
        cros_codecs_decode(&full_path, input_format);
    }
}
