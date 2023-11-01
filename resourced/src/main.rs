// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod config;
mod cpu_utils;
mod dbus;
mod feature;
mod memory;
mod power;
mod psi;
mod qos;
mod vm_concierge_client;
mod vm_memory_management_client;

#[cfg(test)]
mod test_utils;

#[cfg(target_arch = "x86_64")]
mod cgroup_x86_64;

#[cfg(target_arch = "x86_64")]
mod gpu_freq_scaling;

#[cfg(target_arch = "x86_64")]
mod cpu_scaling;

#[cfg(feature = "vm_grpc")]
mod vm_grpc;

use anyhow::bail;
use anyhow::Result;
use libchromeos::panic_handler::install_memfd_handler;
use libchromeos::syslog;
use log::error;
use log::info;
use tokio::runtime::Builder;

const IDENT: &str = "resourced";

fn main() -> Result<()> {
    install_memfd_handler();

    // Initialize syslog. The default log level is info (debug! and trace! are ignored).
    // You can change the log level with log::set_max_level().
    if let Err(e) = syslog::init(IDENT.to_string(), false /* log_to_stderr */) {
        bail!("Failed to initiailize syslog: {}", e);
    }

    info!("Starting resourced");

    if let Err(err) = memory::init_memory_configs() {
        error!("Failed to initialize memory configs: {}", err);
    }

    if let Err(err) = feature::init() {
        error!("Failed to initialize feature: {}", err);
    }

    #[cfg(target_arch = "x86_64")]
    cgroup_x86_64::init()?;

    let rt = Builder::new_current_thread().enable_all().build()?;
    if let Err(err) = rt.block_on(dbus::service_main()) {
        error!("The D-Bus service main returns error: {}", err);
    }

    Ok(())
}
