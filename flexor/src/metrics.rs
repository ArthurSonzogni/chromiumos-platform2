// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::util;
use anyhow::Result;
use nix::sys::stat::Mode;
use std::{os::unix::fs::PermissionsExt, path::Path};

const FLEX_INSTALL_METRICS_FILEPATH: &str = "unencrypted/install_metrics";
const FLEXOR_INSTALL_TYPE: &str = "flexor";

// User ID and Group ID for the flex_hwis user in ChromeOS, which sends an install type metric.
const FLEX_HWIS_UID: u32 = 20207;
const FLEX_HWIS_GID: u32 = 20207;

// File and folder permissions for the install type metric.
const INSTALL_METRICS_DIR_PERM: u32 = 0o755;
const INSTALL_TYPE_FILE_PERM: u32 = 0o644;

/// Writes a file indicating that we installed using flexor. This will be used for metrics.
///
/// Returns `Err` if the file couldn't be written.
pub fn write_install_method_to_stateful(stateful: &Path) -> Result<()> {
    let install_metrics_path = stateful.join(FLEX_INSTALL_METRICS_FILEPATH);
    nix::unistd::mkdir(
        &install_metrics_path,
        Mode::from_bits(INSTALL_METRICS_DIR_PERM).unwrap(),
    )?;

    let install_type_path = install_metrics_path.join("install_type");
    std::fs::write(&install_type_path, FLEXOR_INSTALL_TYPE)?;

    // Set correct owner for both the new `install_metrics` folder and `install_type` file.
    util::set_owner(&install_metrics_path, FLEX_HWIS_UID, FLEX_HWIS_GID)?;
    util::set_owner(&install_type_path, FLEX_HWIS_UID, FLEX_HWIS_GID)?;

    // Set perms on the file itself.
    let mut perm = std::fs::metadata(&install_type_path)?.permissions();
    perm.set_mode(INSTALL_TYPE_FILE_PERM);
    std::fs::set_permissions(install_type_path, perm)?;

    Ok(())
}
