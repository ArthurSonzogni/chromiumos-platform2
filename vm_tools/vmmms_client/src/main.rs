// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod client;
mod vmmms_socket;

use anyhow::Result;
use libchromeos::syslog;
use log::error;
use log::info;
use std::time::Duration;
use vsock::VMADDR_CID_HOST;

use client::VmmmsClient;
use vmmms_socket::VmmmsSocket;

const VM_MEMORY_MANAGEMENT_RECLAIM_SERVER_PORT: u32 = 7782;
const IDENT: &str = "vmmms_client";

fn main() -> Result<()> {
    if let Err(e) = syslog::init(IDENT.to_string(), true /* log_to_stderr */) {
        error!("Failed to initialize syslog: {:?}", e);
    }

    let mut reclaim_client: VmmmsClient = VmmmsClient::new(VmmmsSocket::new(
        VMADDR_CID_HOST,
        VM_MEMORY_MANAGEMENT_RECLAIM_SERVER_PORT,
        Some(Duration::from_secs(5)),
    )?)?;

    info!("VmMemoryManagementClient connection established");

    loop {
        reclaim_client.handle_reclaim_socket_readable()?;
        info!("Successfully handled MGLRU request");
    }
}
