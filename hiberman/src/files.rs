// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! High level support for creating and opening the files used by hibernate.

use std::fs::create_dir;
use std::fs::remove_file;
use std::fs::OpenOptions;
use std::path::Path;
use std::path::PathBuf;

use anyhow::Context;
use anyhow::Result;
use lazy_static::lazy_static;
use log::warn;

/// Define the directory where hibernate state files are kept.
pub const HIBERMETA_DIR: &str = "/mnt/hibermeta";
/// Define the ramfs location where ephemeral files are stored that should not
/// persist across even an unexpected reboot.
pub const TMPFS_DIR: &str = "/run/hibernate/";
/// Define the name of the token file indicating resume is in progress. Note:
/// Services outside of hiberman use this file, so don't change this name
/// carelessly.
const RESUME_IN_PROGRESS_FILE: &str = "resume_in_progress";

lazy_static! {
    /// Define the path of the file with the (obfuscated) account id of the user
    /// who was logged in when the system hibernated.
    pub static ref HIBERNATING_USER_FILE: PathBuf = {
        let path = Path::new("/var/lib/hiberman");
        path.join("hibernating_user")
    };

    /// Define the path of the file with the size of the hibernate image.
    pub static ref HIBERIMAGE_SIZE_FILE: PathBuf = {
        let path = Path::new(HIBERMETA_DIR);
        path.join("hiberimage_size")
    };
}

/// Add the resuming file token that other services can check to quickly see if
/// a resume is in progress.
pub fn create_resume_in_progress_file() -> Result<()> {
    if !Path::new(TMPFS_DIR).exists() {
        create_dir(TMPFS_DIR).context("Cannot create tmpfs directory")?;
    }

    let rip_path = Path::new(TMPFS_DIR).join(RESUME_IN_PROGRESS_FILE);
    if rip_path.exists() {
        warn!("{} unexpectedly already exists", rip_path.display());
    }

    OpenOptions::new()
        .write(true)
        .create(true)
        .open(rip_path)
        .context("Failed to create resume token file")?;

    Ok(())
}

/// Remove the resume_in_progress file if it exists. A result is not returned
/// because besides logging (done here) there's really no handling of this error
/// that could be done.
pub fn remove_resume_in_progress_file() {
    let rip_path = Path::new(TMPFS_DIR).join(RESUME_IN_PROGRESS_FILE);
    if rip_path.exists() {
        if let Err(e) = remove_file(&rip_path) {
            warn!("Failed to remove {}: {}", rip_path.display(), e);
            if rip_path.exists() {
                warn!("{} still exists!", rip_path.display());
            }
        }
    }
}
