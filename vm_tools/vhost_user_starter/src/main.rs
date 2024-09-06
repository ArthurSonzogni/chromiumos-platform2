// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod dserver;

use anyhow::bail;
use anyhow::Ok;
use anyhow::Result;
use libchromeos::syslog;
use log::info;

const IDENT: &str = "vhost_user_starter";

fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();
    // Initialize syslog. The default log level is info (debug! and trace! are ignored).
    if let Err(e) = syslog::init(IDENT.to_string(), false /* log_to_stderr */) {
        bail!("Failed to initialize syslog: {}", e);
    }

    info!("Starting vhost_user_starter");

    if let Err(e) = dserver::service_main() {
        bail!("Server error: {:#}", e);
    }

    Ok(())
}
