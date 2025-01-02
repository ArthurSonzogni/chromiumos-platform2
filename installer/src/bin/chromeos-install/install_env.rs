// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::platform::Platform;
use crate::process_util::Environment;
use anyhow::{Context, Result};
use log::debug;
use nix::mount::MntFlags;
use serde::Deserialize;
use std::collections::BTreeMap;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Temporary mount point path.
const TMP_MNT_PATH: &str = "/tmp/install-mount-point";

/// Flags for `umount2` to forcibly unmount a filesystem. Add
/// `MNT_DETACH` to ensure that FUSE filesystems are correctly
/// unmounted. This matches the flags used in cros-disks.
const FORCE_UNMOUNT_FLAGS: MntFlags = MntFlags::MNT_FORCE.union(MntFlags::MNT_DETACH);

const PARTITION_VARS_PATH: &str = "/usr/sbin/partition_vars.json";

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

/// Unmount anything mounted under `/media/*/*`. Errors are ignored.
pub fn unmount_media(platform: &dyn Platform) {
    let mount_points = get_media_mount_points(platform);

    for path in mount_points {
        if let Err(err) = platform.unmount(&path, FORCE_UNMOUNT_FLAGS) {
            debug!("{err}");
        }
    }
}

/// Get all directories in `/media/*/*`.
///
/// Note that these directories are not necessarily in use as mount
/// points.
///
/// Errors are ignored.
fn get_media_mount_points(platform: &dyn Platform) -> Vec<PathBuf> {
    let mut paths = Vec::new();
    for entry1 in get_directory_contents(&platform.root().join("media")) {
        for entry2 in get_directory_contents(&entry1) {
            if entry2.is_dir() {
                paths.push(entry2);
            }
        }
    }
    paths
}

/// Get all direct children of `dir`.
///
/// This returns a list of file paths within `dir`. Each path is a
/// direntry name joined to `dir`, so if `dir` is an absolute path then
/// the output paths will also be absolute.
///
/// Note that this is not recursive; contents of directories in `dir`
/// are not included in the output.
///
/// Errors are ignored.
fn get_directory_contents(dir: &Path) -> Vec<PathBuf> {
    match fs_err::read_dir(dir) {
        Ok(dir) => dir
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .collect(),
        Err(err) => {
            debug!("{err}");
            Vec::new()
        }
    }
}

/// Read and parse `path` as a partition vars file.
fn get_gpt_base_vars_from(path: &Path) -> Result<Environment> {
    #[derive(Deserialize)]
    struct PartitionVars {
        load_base_vars: BTreeMap<String, String>,
    }

    let json = fs_err::read_to_string(path)?;
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
    if let Err(err) = fs_err::create_dir(path) {
        if err.kind() != ErrorKind::AlreadyExists {
            return Err(err.into());
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
    use fs_err as fs;
    use std::process::Output;

    /// Test that `get_directory_contents` successfully gets all
    /// children of a directory.
    #[test]
    fn test_get_directory_contents() {
        let tmpdir = tempfile::tempdir().unwrap();
        let tmpdir = tmpdir.path();

        let child_dir = tmpdir.join("dir");
        let child_file = tmpdir.join("file");
        fs::create_dir(&child_dir).unwrap();
        fs::write(&child_file, "").unwrap();

        let mut contents = get_directory_contents(tmpdir);
        contents.sort();
        assert_eq!(contents, [child_dir, child_file]);
    }

    /// Test that `get_media_mount_points` successfully gets potential
    /// mount points, and correctly ignores errors.
    #[test]
    fn test_get_media_mount_points() {
        let tmpdir = tempfile::tempdir().unwrap();
        let tmpdir = tmpdir.path();

        let mut platform = MockPlatform::new();
        platform.expect_root_path(tmpdir);

        // Empty: media does not exist.
        assert!(get_media_mount_points(&platform).is_empty());

        // Create media/removable dir.
        let media = tmpdir.join("media");
        fs::create_dir(&media).unwrap();
        let removable = media.join("removable");
        fs::create_dir(&removable).unwrap();

        // Empty: media/removable is empty.
        assert!(get_media_mount_points(&platform).is_empty());

        // Empty: media/removable/file is not a directory.
        fs::write(removable.join("file"), "").unwrap();
        assert!(get_media_mount_points(&platform).is_empty());

        // Create potential mount point directories.
        let mnt1 = removable.join("mnt1");
        let mnt2 = removable.join("mnt2");
        fs::create_dir(&mnt1).unwrap();
        fs::create_dir(&mnt2).unwrap();

        // Assert that the mount points are found.
        let mut found = get_media_mount_points(&platform);
        found.sort();
        assert_eq!(found, [mnt1, mnt2]);
    }

    #[test]
    fn test_unmount_media() {
        let tmpdir = tempfile::tempdir().unwrap();
        let tmpdir = tmpdir.path();

        let mnt1 = tmpdir.join("media/removable/sdc");
        fs::create_dir_all(&mnt1).unwrap();

        let mut platform = MockPlatform::new();
        platform.expect_root_path(tmpdir);
        platform
            .expect_unmount()
            .times(1)
            .withf(move |path, flags| path == mnt1 && *flags == FORCE_UNMOUNT_FLAGS)
            .returning(|_, _| Ok(()));

        unmount_media(&platform);
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
