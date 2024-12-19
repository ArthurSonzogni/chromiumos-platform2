// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::platform::{Platform, PlatformImpl};
use crate::process_util::Environment;
use anyhow::{Context, Result};
use log::debug;
use serde::Deserialize;
use std::collections::BTreeMap;
use std::fs;
use std::io::ErrorKind;
use std::path::Path;
use std::process::Command;

/// Temporary mount point path.
const TMP_MNT_PATH: &str = "/tmp/install-mount-point";

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
    get_tool_env_impl(&PlatformImpl)
}

/// Load vars defining the GPT layout of the installed system from
/// `PARTITION_VARS_PATH`.
pub fn get_gpt_base_vars() -> Result<Environment> {
    get_gpt_base_vars_from(Path::new(PARTITION_VARS_PATH))
}

/// Get the `TMPMNT` var. The value is a temporary directory used as a
/// mount point.
///
/// The directory is created if it does not exist.
pub fn get_temporary_mount_var() -> Result<Environment> {
    create_dir_if_needed(Path::new(TMP_MNT_PATH))?;
    let mut env = Environment::new();
    env.insert("TMPMNT", TMP_MNT_PATH.into());
    Ok(env)
}

/// Stop the cros-disks service. Errors are ignored.
pub fn stop_cros_disks(platform: &dyn Platform) {
    let mut cmd = Command::new("initctl");
    cmd.args(["stop", "cros-disks"]);
    // Capture all output to avoid unnecessary log spam.
    if let Err(err) = platform.run_command_and_get_output(cmd) {
        debug!("{err}");
    }
}

/// Check if the `dd` command comes from busybox.
fn is_dd_busybox(platform: &dyn Platform) -> Result<bool> {
    let mut cmd = Command::new("dd");
    cmd.arg("--version");
    let stdout = platform.run_command_and_get_stdout(cmd)?;
    Ok(stdout.contains("BusyBox"))
}

/// Check if the `losetup` command comes from busybox.
fn is_losetup_busybox(platform: &dyn Platform) -> Result<bool> {
    let mut cmd = Command::new("losetup");
    cmd.arg("--version");
    let stdout = platform.run_command_and_get_stdout(cmd)?;
    Ok(stdout.contains("BusyBox"))
}

/// See `get_tool_env` for details.
fn get_tool_env_impl(platform: &dyn Platform) -> Result<Environment> {
    let mut env = Environment::new();

    let is_dd_busybox = is_dd_busybox(platform)?;
    let is_losetup_busybox = is_losetup_busybox(platform)?;

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

/// Create a directory at `path`.
///
/// Returns `Ok` if the directory was successfully created, or if it
/// already exists.
///
/// Returns an error if the directory's parent does not exist, if any
/// other error occurs when creating the directory.
fn create_dir_if_needed(path: &Path) -> Result<()> {
    debug!("creating {}", path.display());
    if let Err(err) = fs::create_dir(path) {
        if err.kind() != ErrorKind::AlreadyExists {
            return Err(err).with_context(|| format!("failed to create {}", path.display()));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::platform::MockPlatform;
    use crate::process_util::ProcessError;
    use anyhow::anyhow;
    use std::process::Output;

    const BUSYBOX_HELP: &str = "BusyBox v1.36.1 (2024-02-04 23:31:36 PST) multi-call binary.";

    /// Test that `get_tool_env_impl` produces the expected vars for a
    /// busybox environment.
    #[test]
    fn test_busybox_env() {
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok(BUSYBOX_HELP.to_owned()));
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Ok(BUSYBOX_HELP.to_owned()));

        assert_eq!(
            get_tool_env_impl(&platform).unwrap().into_vec(),
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
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok("dd (coreutils) 8.32".to_owned()));
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Ok("losetup from util-linux 2.38.1".to_owned()));

        assert_eq!(
            get_tool_env_impl(&platform).unwrap().into_vec(),
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
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Err(anyhow!("dd not found")));

        assert!(get_tool_env_impl(&platform).is_err());
    }

    /// Test that `get_tool_env_impl` propagates errors if `losetup`
    /// can't be run.
    #[test]
    fn test_bad_losetup() {
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "dd")
            .return_once(|_| Ok("dd (coreutils) 8.32".to_owned()));
        platform
            .expect_run_command_and_get_stdout()
            .withf(|cmd| cmd.get_program() == "losetup")
            .return_once(|_| Err(anyhow!("losetup not found")));

        assert!(get_tool_env_impl(&platform).is_err());
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

    /// Test `create_dir_if_needed`.
    #[test]
    fn test_create_dir_if_needed() {
        let tmpdir = tempfile::tempdir().unwrap();

        // Error: parent does not exist.
        assert!(create_dir_if_needed(&tmpdir.path().join("parent/tmpmnt")).is_err());

        // Successfully create tmpmnt.
        let path = tmpdir.path().join("tmpmnt");
        create_dir_if_needed(&path).unwrap();
        assert!(path.exists());

        // Calling it again succeeds; directory already exists.
        create_dir_if_needed(&path).unwrap();
        assert!(path.exists());
    }

    /// Test a successful call to `stop_cros_disks`.
    #[test]
    fn test_stop_cros_disks() {
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_output()
            .withf(|cmd| cmd.get_program() == "initctl")
            .return_once(|_| {
                Ok(Output {
                    stdout: vec![],
                    stderr: vec![],
                    status: Default::default(),
                })
            });
        stop_cros_disks(&platform);
    }

    /// Test that `stop_cros_disks` ignores errors.
    #[test]
    fn test_stop_cros_disks_err() {
        let mut platform = MockPlatform::new();
        platform
            .expect_run_command_and_get_output()
            .withf(|cmd| cmd.get_program() == "initctl")
            .return_once(|_| Err(ProcessError::default()));
        stop_cros_disks(&platform);
    }
}
