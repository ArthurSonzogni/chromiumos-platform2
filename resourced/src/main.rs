// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod config;
mod dbus;
mod gpu_freq_scaling;
mod memory;
mod power;

#[cfg(test)]
mod test;

use std::path::Path;

use anyhow::{bail, Result};
use sys_util::{info, syslog};

fn main() -> Result<()> {
    if let Err(e) = syslog::init() {
        bail!("Failed to initiailize syslog: {}", e);
    }

    info!("Starting resourced");

    let root = Path::new("/");

    let power_preferences_manager = power::DirectoryPowerPreferencesManager {
        root: root.to_path_buf(),
        config_provider: config::DirectoryConfigProvider {
            root: root.to_path_buf(),
        },
        power_source_provider: power::DirectoryPowerSourceProvider { root },
    };

    dbus::service_main(power_preferences_manager)
}
