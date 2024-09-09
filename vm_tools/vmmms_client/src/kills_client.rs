// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Result;
use log::info;
use nix::poll::poll;
use nix::poll::PollFd;
use nix::poll::PollFlags;
use nix::poll::PollTimeout;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::os::fd::AsFd;
use std::time::Duration;
use std::time::Instant;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::ResizePriority;
use system_api::vm_memory_management::VmMemoryManagementPacket;

use crate::vmmms_socket::VmmmsSocket;

pub struct KillsClient {
    pub vmmms_socket: VmmmsSocket,
    pub psi_file: File,
    pub current_sequence_num: u32,
}

impl KillsClient {
    pub fn new(mut vmmms_socket: VmmmsSocket, psi_file_path: &str) -> Result<Self> {
        vmmms_socket.handshake(ConnectionType::CONNECTION_TYPE_KILLS)?;

        const STALL_DURATION: std::time::Duration = Duration::from_millis(100);
        let psi_file = Self::set_trigger(psi_file_path, STALL_DURATION)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
            psi_file: psi_file,
            current_sequence_num: 0,
        })
    }

    fn set_trigger(psi_file_path: &str, stall: std::time::Duration) -> Result<File> {
        let mut psi_file: File = OpenOptions::new()
            .read(true)
            .write(true)
            .open(psi_file_path)?;

        const TARGET: &str = "some";
        // The window size for monitors made by unprivileged users
        // is restricted to multiples of 2s
        const WINDOW_DURATION: std::time::Duration = Duration::from_secs(2);

        if stall > WINDOW_DURATION {
            bail!("stall is longer than window");
        }

        let monitor_config = format!(
            "{} {} {}\0",
            TARGET,
            stall.as_micros(),
            WINDOW_DURATION.as_micros()
        );
        psi_file.write_all(monitor_config.as_bytes())?;

        return Ok(psi_file);
    }

    pub fn handle_psi_notification(&mut self) -> Result<()> {
        const TIMEOUT: std::time::Duration = Duration::from_secs(5);
        const SIZE_KB: u32 = 100 * 1024;

        let kill_decision_start = Instant::now();
        let deadline = kill_decision_start + TIMEOUT;

        self.current_sequence_num = self.current_sequence_num.wrapping_add(1);

        let mut kill_request = VmMemoryManagementPacket::new();
        kill_request.type_ = PacketType::PACKET_TYPE_KILL_REQUEST.into();
        kill_request.mut_kill_decision_request().sequence_num = self.current_sequence_num;
        kill_request.mut_kill_decision_request().size_kb = SIZE_KB;
        kill_request.mut_kill_decision_request().priority =
            ResizePriority::RESIZE_PRIORITY_STALE_CACHED_APP.into();

        self.vmmms_socket.write_packet(kill_request)?;

        let mut received_sequence_num = 0;
        let mut size_freed_kb = 0;

        while Instant::now() < deadline && received_sequence_num != self.current_sequence_num {
            let mut fds = [PollFd::new(self.vmmms_socket.as_fd(), PollFlags::POLLIN)];
            let timeout = match PollTimeout::try_from(deadline - Instant::now()) {
                Err(_e) => continue,
                Ok(timeout) => timeout,
            };
            poll(&mut fds, timeout)?;

            let is_kills_socket_readable = match fds[0].revents() {
                None => continue,
                Some(revents) => revents.intersects(PollFlags::POLLIN),
            };

            if !is_kills_socket_readable {
                continue;
            }

            let mut kill_decision_response = self.vmmms_socket.read_packet()?;
            if kill_decision_response.type_ != PacketType::PACKET_TYPE_KILL_DECISION.into() {
                info!("Received unsupported command on kill decision request.");
                continue;
            };
            if !kill_decision_response.has_kill_decision_response() {
                info!("Kill decision response is empty.");
                continue;
            };

            received_sequence_num = kill_decision_response
                .mut_kill_decision_response()
                .sequence_num;
            size_freed_kb = kill_decision_response
                .kill_decision_response()
                .size_freed_kb;
        }

        info!(
            "{:?} KB of memory was freed by the VM Memory Management Service",
            size_freed_kb
        );

        let mut latency_packet = VmMemoryManagementPacket::new();
        latency_packet.type_ = PacketType::PACKET_TYPE_DECISION_LATENCY.into();
        latency_packet.mut_decision_latency().sequence_num = self.current_sequence_num;
        latency_packet.mut_decision_latency().latency_ms =
            if received_sequence_num != self.current_sequence_num {
                u32::MAX
            } else {
                match (Instant::now() - kill_decision_start).as_millis() {
                    v if v > u32::MAX as u128 => u32::MAX,
                    v => v as u32,
                }
            };

        self.vmmms_socket.write_packet(latency_packet)?;

        return Ok(());
    }
}
