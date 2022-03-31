// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cgroup;
mod common;
mod config;
mod dbus;
mod feature;
mod gpu_freq_scaling;
mod memory;
mod power;

#[cfg(target_arch = "x86_64")]
mod power_x86_64;

#[cfg(test)]
mod test;

use std::path::Path;

use anyhow::{bail, Result};
use libchromeos::panic_handler::install_memfd_handler;
use sys_util::{info, syslog};

fn main() -> Result<()> {
    install_memfd_handler();
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
        power_source_provider: power::DirectoryPowerSourceProvider {
            root: root.to_path_buf(),
        },
        feature_provider: feature::CrOSFeatureProvider::new()?,
        cpuset_manager: cgroup::CgroupCpusetManager::new(root.to_path_buf())?,
    };

    dbus::service_main(power_preferences_manager)
}
