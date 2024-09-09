// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mglru;
mod reclaim_client;
mod vmmms_socket;

use anyhow::Result;
use libchromeos::syslog;
use log::error;
use log::info;
use nix::poll::poll;
use nix::poll::PollFd;
use nix::poll::PollFlags;
use nix::poll::PollTimeout;
use std::os::fd::AsFd;
use std::time::Duration;
use vsock::VMADDR_CID_HOST;

use reclaim_client::ReclaimClient;
use vmmms_socket::VmmmsSocket;

const VM_MEMORY_MANAGEMENT_RECLAIM_SERVER_PORT: u32 = 7782;
const IDENT: &str = "vmmms_client";
const READ_TIMEOUT: Option<Duration> = Some(Duration::from_secs(5));
const MGLRU_ADMIN_FILE_PATH: &str = "/sys/kernel/mm/lru_gen/admin";

fn main() -> Result<()> {
    if let Err(e) = syslog::init(IDENT.to_string(), true /* log_to_stderr */) {
        error!("Failed to initialize syslog: {:?}", e);
    }

    let mut reclaim_client: ReclaimClient = ReclaimClient::new(
        VmmmsSocket::new(
            VMADDR_CID_HOST,
            VM_MEMORY_MANAGEMENT_RECLAIM_SERVER_PORT,
            READ_TIMEOUT,
        )?,
        MGLRU_ADMIN_FILE_PATH,
    )?;

    info!("VmMemoryManagementClient connection established");

    loop {
        let mut fds = [
            PollFd::new(reclaim_client.vmmms_socket.as_fd(), PollFlags::POLLIN),
            PollFd::new(reclaim_client.mglru_file.as_fd(), PollFlags::POLLPRI),
        ];
        poll(&mut fds, PollTimeout::NONE)?;

        let is_reclaim_socket_readable = fds[0]
            .revents()
            .expect("kernel must not set bits except PollFlags")
            .intersects(PollFlags::POLLIN);
        let is_mglru_file_readable = fds[1]
            .revents()
            .expect("kernel must not set bits except PollFlags")
            .intersects(PollFlags::POLLPRI);

        if is_reclaim_socket_readable {
            reclaim_client.handle_reclaim_socket_readable()?;
            info!("Successfully handled MGLRU request");
        }
        if is_mglru_file_readable {
            info!("Received mglru notification");
            reclaim_client.handle_mglru_notification()?;
        }
    }
}
