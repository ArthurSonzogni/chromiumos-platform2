// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod kills_client;
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
use std::time::Instant;
use system_api::vm_memory_management::ResizePriority;
use vsock::VMADDR_CID_HOST;

use kills_client::KillsClient;
use reclaim_client::ReclaimClient;
use vmmms_socket::VmmmsSocket;

const VM_MEMORY_MANAGEMENT_RECLAIM_SERVER_PORT: u32 = 7782;
const VM_MEMORY_MANAGEMENT_KILLS_SERVER_PORT: u32 = 7783;
const IDENT: &str = "vmmms_client";
const READ_TIMEOUT: Option<Duration> = Some(Duration::from_secs(5));
const MGLRU_ADMIN_FILE_PATH: &str = "/sys/kernel/mm/lru_gen/admin";
const PSI_FILE_PATH: &str = "/proc/pressure/memory";

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

    let mut kills_client: KillsClient = KillsClient::new(
        VmmmsSocket::new(
            VMADDR_CID_HOST,
            VM_MEMORY_MANAGEMENT_KILLS_SERVER_PORT,
            READ_TIMEOUT,
        )?,
        PSI_FILE_PATH,
    )?;

    info!("VmMemoryManagementClient connection established");

    loop {
        let mut fds = [
            PollFd::new(reclaim_client.vmmms_socket.as_fd(), PollFlags::POLLIN),
            PollFd::new(reclaim_client.mglru_file.as_fd(), PollFlags::POLLPRI),
            PollFd::new(kills_client.low_psi_monitor.as_fd(), PollFlags::POLLPRI),
            PollFd::new(kills_client.medium_psi_monitor.as_fd(), PollFlags::POLLPRI),
            PollFd::new(kills_client.high_psi_monitor.as_fd(), PollFlags::POLLPRI),
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
        let received_low_psi_notification = fds[2]
            .revents()
            .expect("kernel must not set bits except PollFlags")
            .intersects(PollFlags::POLLPRI);
        let received_medium_psi_notification = fds[3]
            .revents()
            .expect("kernel must not set bits except PollFlags")
            .intersects(PollFlags::POLLPRI);
        let received_high_psi_notification = fds[4]
            .revents()
            .expect("kernel must not set bits except PollFlags")
            .intersects(PollFlags::POLLPRI);

        if is_reclaim_socket_readable {
            reclaim_client.handle_reclaim_socket_readable()?;
        }
        if is_mglru_file_readable {
            reclaim_client.handle_mglru_notification()?;
        }
        if received_low_psi_notification {
            kills_client.handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_CACHED_APP,
                Instant::now(),
            )?;
        }
        if received_medium_psi_notification {
            kills_client.handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP,
                Instant::now(),
            )?;
        }
        if received_high_psi_notification {
            kills_client.handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_FOCUSED_APP,
                Instant::now(),
            )?;
        }
    }
}
