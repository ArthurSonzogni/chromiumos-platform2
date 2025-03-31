// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::disk_util;
use crate::platform::Platform;
use anyhow::Result;
use libinstall::process_util::Environment;
use std::path::PathBuf;

/// Where to install from: where to pull the disk layout, files, mbr bootloader, etc. from.
pub enum InstallSource {
    /// An install from the root of the running OS.
    RunningOS {
        /// The path to the root, or `None` if we don't intend to use it (i.e. when --skip_rootfs is
        /// passed).
        root_device: Option<PathBuf>,
    },
    /// An Install from a ChromiumOS image on the filesystem. Wraps the path to the image.
    PayloadImage(PathBuf),
}

impl InstallSource {
    /// Choose a source based on the relevant command-line arguments.
    ///
    /// If a payload image is passed we'll use that as our source, otherwise
    /// we use the running OS.
    pub fn from_args(
        platform: &dyn Platform,
        skip_rootfs: bool,
        payload_image: Option<PathBuf>,
    ) -> Result<Self> {
        if let Some(payload_image) = payload_image {
            Ok(InstallSource::PayloadImage(payload_image))
        } else if skip_rootfs {
            Ok(InstallSource::RunningOS { root_device: None })
        } else {
            let block_dev = disk_util::get_root_disk_device_path(platform)?;
            Ok(InstallSource::RunningOS {
                root_device: Some(block_dev),
            })
        }
    }

    /// Format relevant data for passing as env var to the shell script.
    pub fn to_env(&self) -> Environment {
        let mut result = Environment::new();

        let src_str = self.source_device().into_os_string();
        result.insert("SRC", src_str);
        let root_str = self.root().into_os_string();
        result.insert("ROOT", root_str);

        // This is temporary until we pull the rest of check_payload_image into rust.
        if let InstallSource::PayloadImage(payload_image) = self {
            // Just pass this through.
            result.insert("FLAGS_payload_image", payload_image.clone().into());
        };

        result
    }

    /// The path to the block device we're installing from, e.g. `/dev/sda` or `/dev/loop1`
    pub fn source_device(&self) -> PathBuf {
        match self {
            InstallSource::RunningOS { root_device } => {
                if let Some(path) = root_device {
                    path.clone()
                } else {
                    PathBuf::new()
                }
            }
            InstallSource::PayloadImage(_) => {
                // This will be implemented in a future commit.
                // For now check_payload_image will overwrite it.
                PathBuf::new()
            }
        }
    }

    /// A path prefix to the root filesystem we're installing from,
    /// which will be an empty path if the filesystem is the same as what we're running from.
    pub fn root(&self) -> PathBuf {
        match self {
            InstallSource::RunningOS { .. } => PathBuf::new(),
            InstallSource::PayloadImage(_) => {
                // This will be implemented in a future commit.
                // For now check_payload_image will overwrite it.
                PathBuf::new()
            }
        }
    }
}
