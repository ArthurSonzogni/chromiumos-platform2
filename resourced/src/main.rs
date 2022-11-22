// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod config;
mod dbus;
mod gpu_freq_scaling;
mod memory;
mod power;

#[cfg(target_arch = "x86_64")]
mod cgroup_x86_64;

#[cfg(target_arch = "x86_64")]
mod cpu_scaling;

use std::path::Path;

use anyhow::{bail, Result};
use libchromeos::panic_handler::install_memfd_handler;
use libchromeos::sys::info;
use libchromeos::syslog;

const IDENT: &str = "resourced";

fn main() -> Result<()> {
    install_memfd_handler();
    if let Err(e) = syslog::init(IDENT.to_string(), false /* log_to_stderr */) {
        bail!("Failed to initiailize syslog: {}", e);
    }

    info!("Starting resourced");

    #[cfg(target_arch = "x86_64")]
    cgroup_x86_64::init()?;

    let root = Path::new("/");

    let power_preferences_manager = power::DirectoryPowerPreferencesManager {
        root: root.to_path_buf(),
        config_provider: config::DirectoryConfigProvider {
            root: root.to_path_buf(),
        },
        power_source_provider: power::DirectoryPowerSourceProvider {
            root: root.to_path_buf(),
        },
    };

    dbus::service_main(power_preferences_manager)
}
