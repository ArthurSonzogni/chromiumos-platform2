// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use libchromeos::panic_handler;
use log::info;

static FLEXOR_TAG: &'static str = "flexor";

fn main() -> Result<()> {
    // Setup the panic handler.
    panic_handler::install_memfd_handler();
    // For now log everything.
    log::set_max_level(log::LevelFilter::Info);
    if let Err(err) = stderrlog::new()
        .module(FLEXOR_TAG)
        .show_module_names(true)
        .verbosity(log::max_level())
        .init()
    {
        eprintln!("Failed to initialize logger: {err}");
    }

    info!("Hello from Flexor!");

    Ok(())
}
