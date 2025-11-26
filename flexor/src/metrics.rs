// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::util;
use anyhow::Result;
use log::info;
use nix::sys::stat::Mode;
use std::{fs, os::unix::fs::PermissionsExt, path::Path};

/// The file where we store the method used to install.
pub const FLEXOR_INSTALL_TYPE_FILE: &str = "install_type";

const FLEX_INSTALL_METRICS_FILEPATH: &str = "unencrypted/install_metrics";
const FLEXOR_DEFAULT_INSTALL_METHOD: &str = "flexor";

// User ID and Group ID for the flex_hwis user in ChromeOS, which sends an install type metric.
const FLEX_HWIS_UID: u32 = 20207;
const FLEX_HWIS_GID: u32 = 20207;

// File and folder permissions for the install type metric.
const INSTALL_METRICS_DIR_PERM: u32 = 0o755;
const INSTALL_TYPE_FILE_PERM: u32 = 0o644;

/// Read whether flexor is being used for Remote Deploy or Mass Deploy, or return a default.
///
/// Flexor can be used for either Remote or Mass Deploy, and we want to distinguish between the two
/// in our metrics. Check the install_type file for a valid install method, returning the default
/// if there are errors reading it.
fn get_install_method(source: &Path) -> String {
    let default = FLEXOR_DEFAULT_INSTALL_METHOD.to_string();

    let method = if let Ok(method) = fs::read_to_string(source.join(FLEXOR_INSTALL_TYPE_FILE)) {
        method
    } else {
        info!("Couldn't read install method for metrics, using default.");
        return default;
    };

    // Only use it if it matches one of the values we recognize in flex_device_metrics.
    if method != "mass-deploy" && method != "remote-deploy" {
        info!("Unrecognized install method, using default instead.");
        return default;
    }

    method
}

/// Writes a file indicating that we installed using flexor. This will be used for metrics.
///
/// Returns `Err` if the file couldn't be written.
pub fn write_install_method_to_stateful(source: &Path, stateful: &Path) -> Result<()> {
    let method = get_install_method(source);

    let install_metrics_path = stateful.join(FLEX_INSTALL_METRICS_FILEPATH);
    nix::unistd::mkdir(
        &install_metrics_path,
        Mode::from_bits(INSTALL_METRICS_DIR_PERM).unwrap(),
    )?;

    let install_type_path = install_metrics_path.join(FLEXOR_INSTALL_TYPE_FILE);
    std::fs::write(&install_type_path, method)?;

    // Set correct owner for both the new `install_metrics` folder and `install_type` file.
    util::set_owner(&install_metrics_path, FLEX_HWIS_UID, FLEX_HWIS_GID)?;
    util::set_owner(&install_type_path, FLEX_HWIS_UID, FLEX_HWIS_GID)?;

    // Set perms on the file itself.
    let mut perm = std::fs::metadata(&install_type_path)?.permissions();
    perm.set_mode(INSTALL_TYPE_FILE_PERM);
    std::fs::set_permissions(install_type_path, perm)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_get_install_method() -> Result<()> {
        let tempdir = tempfile::tempdir()?;
        let dir_path = tempdir.path();
        let file_path = dir_path.join(FLEXOR_INSTALL_TYPE_FILE);

        // No file
        assert_eq!(get_install_method(dir_path), FLEXOR_DEFAULT_INSTALL_METHOD);

        // Empty file
        std::fs::write(&file_path, "")?;
        assert_eq!(get_install_method(dir_path), FLEXOR_DEFAULT_INSTALL_METHOD);

        // Junk
        std::fs::write(&file_path, "future-deploy")?;
        assert_eq!(get_install_method(dir_path), FLEXOR_DEFAULT_INSTALL_METHOD);

        // Mass Deploy
        std::fs::write(&file_path, "mass-deploy")?;
        assert_eq!(get_install_method(dir_path), "mass-deploy");

        // Remote Deploy
        std::fs::write(&file_path, "remote-deploy")?;
        assert_eq!(get_install_method(dir_path), "remote-deploy");

        Ok(())
    }
}
