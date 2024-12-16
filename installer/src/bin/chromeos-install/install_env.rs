// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::process_util::{Environment, RunCommand, RunCommandImpl};
use anyhow::{Context, Result};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;
use std::process::Command;

// Env var names. Must match chromeos-install.sh.
const BUSYBOX_DD_FOUND: &str = "BUSYBOX_DD_FOUND";
const LOSETUP_PATH: &str = "LOSETUP_PATH";

const PARTITION_VARS_PATH: &str = "/usr/sbin/partition_vars.json";

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

/// Load vars defining the GPT layout of the installed system from
/// `PARTITION_VARS_PATH`.
pub fn get_gpt_base_vars() -> Result<Environment> {
    get_gpt_base_vars_from(Path::new(PARTITION_VARS_PATH))
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

/// Read and parse `path` as a partition vars file.
fn get_gpt_base_vars_from(path: &Path) -> Result<Environment> {
    #[derive(Deserialize)]
    struct PartitionVars {
        load_base_vars: BTreeMap<String, String>,
    }

    let json =
        fs::read_to_string(path).with_context(|| format!("failed to read {}", path.display()))?;
    let partition_vars: PartitionVars = serde_json::from_str(&json)
        .with_context(|| format!("failed to parse {}", path.display()))?;

    let mut env = Environment::new();
    env.extend(
        partition_vars
            .load_base_vars
            .into_iter()
            .map(|(k, v)| (k, v.into())),
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
                (BUSYBOX_DD_FOUND.into(), "true".into()),
                (LOSETUP_PATH.into(), "/bin/losetup".into())
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
                (BUSYBOX_DD_FOUND.into(), "false".into()),
                (LOSETUP_PATH.into(), "losetup".into())
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

    /// Test that `get_gpt_base_vars_from` reads a valid
    /// partition_vars.json file correctly.
    #[test]
    fn test_get_gpt_base_vars_from() {
        let tmpdir = tempfile::tempdir().unwrap();
        let path = tmpdir.path().join("partition_vars.json");
        fs::write(
            &path,
            // This test data is from a valid file, with most vars
            // removed for brevity.
            r#"
{
  "load_base_vars": {
    "PARTITION_NUM_ROOT_A": "3",
    "PARTITION_SIZE_3": "4294967296"
  },
  "load_partition_vars": {
    "PARTITION_NUM_ROOT_A": "3",
    "PARTITION_SIZE_3": "2516582400"
  }
}
"#,
        )
        .unwrap();
        assert_eq!(
            get_gpt_base_vars_from(&path).unwrap().into_vec(),
            [
                ("PARTITION_NUM_ROOT_A".into(), "3".into()),
                ("PARTITION_SIZE_3".into(), "4294967296".into()),
            ]
        );
    }

    /// Test that `get_gpt_base_vars_from` propagates if the file is not
    /// found or contains invalid data.
    #[test]
    fn test_get_gpt_base_vars_from_errors() {
        let tmpdir = tempfile::tempdir().unwrap();
        let path = tmpdir.path().join("partition_vars.json");

        // Path does not exist.
        assert!(get_gpt_base_vars_from(&path).is_err());

        // File contains invalid data.
        fs::write(&path, "invalid data").unwrap();
        assert!(get_gpt_base_vars_from(&path).is_err());
    }
}
