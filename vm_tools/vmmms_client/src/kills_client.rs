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
use std::path::Path;
use std::time::Duration;
use std::time::Instant;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::ResizePriority;
use system_api::vm_memory_management::VmMemoryManagementPacket;

use crate::vmmms_socket::VmmmsSocket;

pub struct KillsClient {
    pub vmmms_socket: VmmmsSocket,
    pub low_psi_monitor: File,
    pub medium_psi_monitor: File,
    pub high_psi_monitor: File,
    pub current_sequence_num: u32,
    last_send_resize_priority: ResizePriority,
    last_send_timestamp: Instant,
}

impl KillsClient {
    pub fn new(mut vmmms_socket: VmmmsSocket, psi_file_path: &Path) -> Result<Self> {
        vmmms_socket.handshake(ConnectionType::CONNECTION_TYPE_KILLS)?;

        const LOW_STALL_DURATION: Duration = Duration::from_millis(200);
        let low_psi_monitor = Self::set_trigger(psi_file_path, LOW_STALL_DURATION)?;

        const MEDIUM_STALL_DURATION: Duration = Duration::from_millis(400);
        let medium_psi_monitor = Self::set_trigger(psi_file_path, MEDIUM_STALL_DURATION)?;

        const HIGH_STALL_DURATION: Duration = Duration::from_millis(600);
        let high_psi_monitor = Self::set_trigger(psi_file_path, HIGH_STALL_DURATION)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
            low_psi_monitor: low_psi_monitor,
            medium_psi_monitor: medium_psi_monitor,
            high_psi_monitor: high_psi_monitor,
            current_sequence_num: 0,
            last_send_resize_priority: ResizePriority::RESIZE_PRIORITY_STALE_CACHED_APP,
            last_send_timestamp: Instant::now(),
        })
    }

    fn set_trigger(psi_file_path: &Path, stall: Duration) -> Result<File> {
        let mut psi_file: File = OpenOptions::new()
            .read(true)
            .write(true)
            .open(psi_file_path)?;

        const TARGET: &str = "some";
        // The window size for monitors made by unprivileged users
        // is restricted to multiples of 2s
        const WINDOW_DURATION: Duration = Duration::from_secs(2);

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

    fn get_cooldown_duration(
        last_send_resize_priority: ResizePriority,
        resize_priority: ResizePriority,
    ) -> Duration {
        const LONG_COOLDOWN: Duration = Duration::from_secs(5);
        const MEDIUM_COOLDOWN: Duration = Duration::from_secs(2);
        const SHORT_COOLDOWN: Duration = Duration::from_secs(1);

        match resize_priority {
            ResizePriority::RESIZE_PRIORITY_CACHED_APP => match last_send_resize_priority {
                ResizePriority::RESIZE_PRIORITY_CACHED_APP
                | ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP
                | ResizePriority::RESIZE_PRIORITY_FOCUSED_APP => LONG_COOLDOWN,
                _ => Duration::from_secs(0),
            },
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP => match last_send_resize_priority {
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP
                | ResizePriority::RESIZE_PRIORITY_FOCUSED_APP => MEDIUM_COOLDOWN,
                _ => Duration::from_secs(0),
            },
            ResizePriority::RESIZE_PRIORITY_FOCUSED_APP => match last_send_resize_priority {
                ResizePriority::RESIZE_PRIORITY_FOCUSED_APP => SHORT_COOLDOWN,
                _ => Duration::from_secs(0),
            },
            _ => Duration::from_secs(0),
        }
    }

    pub fn handle_psi_notification(
        &mut self,
        resize_priority: ResizePriority,
        current_time: Instant,
    ) -> Result<()> {
        const TIMEOUT: Duration = Duration::from_secs(5);
        const SIZE_KB: u32 = 100 * 1024;

        // This aims to prevent simultaneously sending lower level of ResizePriority
        if current_time
            < self.last_send_timestamp
                + Self::get_cooldown_duration(self.last_send_resize_priority, resize_priority)
        {
            return Ok(());
        }
        self.last_send_resize_priority = resize_priority;
        self.last_send_timestamp = current_time;

        let kill_decision_start = Instant::now();
        let deadline = kill_decision_start + TIMEOUT;

        self.current_sequence_num = self.current_sequence_num.wrapping_add(1);

        let mut kill_request = VmMemoryManagementPacket::new();
        kill_request.type_ = PacketType::PACKET_TYPE_KILL_REQUEST.into();
        kill_request.mut_kill_decision_request().sequence_num = self.current_sequence_num;
        kill_request.mut_kill_decision_request().size_kb = SIZE_KB;
        kill_request.mut_kill_decision_request().priority = resize_priority.into();

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

            let kill_decision_response = self.vmmms_socket.read_packet()?;
            if kill_decision_response.type_ != PacketType::PACKET_TYPE_KILL_DECISION.into() {
                info!("Received unsupported command on kill decision request.");
                continue;
            };
            if !kill_decision_response.has_kill_decision_response() {
                info!("Kill decision response is empty.");
                continue;
            };

            received_sequence_num = kill_decision_response
                .kill_decision_response()
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

#[cfg(test)]
mod tests {
    use crate::vmmms_socket::tests::write_handshake_response;
    use crate::KillsClient;
    use crate::VmmmsSocket;
    use crate::READ_TIMEOUT;
    use std::time::Duration;
    use std::time::Instant;
    use system_api::vm_memory_management::ConnectionType;
    use system_api::vm_memory_management::PacketType;
    use system_api::vm_memory_management::ResizePriority;
    use system_api::vm_memory_management::VmMemoryManagementPacket;
    use tempfile::NamedTempFile;

    fn write_kill_decision_response(
        server_vsock_test: &mut VmmmsSocket,
        sequence_num: u32,
        size_freed_kb: u32,
    ) {
        let mut kill_decision_response = VmMemoryManagementPacket::new();
        kill_decision_response.type_ = PacketType::PACKET_TYPE_KILL_DECISION.into();
        kill_decision_response
            .mut_kill_decision_response()
            .sequence_num = sequence_num;
        kill_decision_response
            .mut_kill_decision_response()
            .size_freed_kb = size_freed_kb;
        server_vsock_test
            .write_packet(kill_decision_response)
            .expect("Should be able to write packet");
    }

    #[test]
    fn connection_succeeds() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let _kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");

        let handshake_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            handshake_packet.type_,
            PacketType::PACKET_TYPE_HANDSHAKE.into()
        );
        assert_eq!(
            handshake_packet.handshake().type_,
            ConnectionType::CONNECTION_TYPE_KILLS.into()
        );
    }

    #[test]
    fn no_kill_decision_response() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();

        kills_client_test
            .handle_psi_notification(ResizePriority::RESIZE_PRIORITY_CACHED_APP, Instant::now())
            .expect("Should be able to handle psi notification");

        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_CACHED_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 1);
        assert!(latency_packet.decision_latency().latency_ms == u32::MAX);
    }

    #[test]
    fn several_kill_decision_response() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();

        kills_client_test
            .handle_psi_notification(ResizePriority::RESIZE_PRIORITY_CACHED_APP, Instant::now())
            .expect("Should be able to handle psi notification");
        let _kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        let _latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");

        write_kill_decision_response(&mut server_vsock_test, 1, 100);
        write_kill_decision_response(&mut server_vsock_test, 2, 100);
        kills_client_test
            .handle_psi_notification(ResizePriority::RESIZE_PRIORITY_CACHED_APP, Instant::now())
            .expect("Should be able to handle psi notification");

        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_CACHED_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 2);
        assert!(latency_packet.decision_latency().latency_ms < u32::MAX);
    }

    #[test]
    fn invalid_kill_decision_response() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();

        let mut ivalid_kill_decision_response = VmMemoryManagementPacket::new();
        ivalid_kill_decision_response.type_ = PacketType::PACKET_TYPE_MGLRU_REQUEST.into();
        server_vsock_test
            .write_packet(ivalid_kill_decision_response)
            .expect("Should be able to write packet");

        kills_client_test
            .handle_psi_notification(ResizePriority::RESIZE_PRIORITY_CACHED_APP, Instant::now())
            .expect("Should be able to handle psi notification");
        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_CACHED_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 1);
        assert!(latency_packet.decision_latency().latency_ms == u32::MAX);
    }

    #[test]
    fn handle_psi_notification_succeeds() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        write_kill_decision_response(&mut server_vsock_test, 1, 100);

        kills_client_test
            .handle_psi_notification(ResizePriority::RESIZE_PRIORITY_CACHED_APP, Instant::now())
            .expect("Should be able to handle psi notification");
        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_CACHED_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 1);
        assert!(latency_packet.decision_latency().latency_ms < u32::MAX);
    }

    #[test]
    fn block_lower_priority_of_kill_request() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        write_kill_decision_response(&mut server_vsock_test, 1, 100);
        let first_notification_timestamp = Instant::now();
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_FOCUSED_APP,
                first_notification_timestamp,
            )
            .expect("Should be able to handle psi notification");
        let _kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        let _latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP,
                first_notification_timestamp,
            )
            .expect("Should be able to handle psi notification");

        assert!(server_vsock_test.read_packet().is_err());
    }

    #[test]
    fn unblock_lower_priority_of_kill_request_after_cooldown() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        write_kill_decision_response(&mut server_vsock_test, 1, 100);
        let first_notification_timestamp = Instant::now();
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_FOCUSED_APP,
                first_notification_timestamp,
            )
            .expect("Should be able to handle psi notification");
        let _kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        let _latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        const MEDIUM_COOLDOWN: Duration = Duration::from_secs(2);
        write_kill_decision_response(&mut server_vsock_test, 2, 100);
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP,
                first_notification_timestamp
                    .checked_add(MEDIUM_COOLDOWN)
                    .unwrap(),
            )
            .expect("Should be able to handle psi notification");

        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 2);
        assert!(latency_packet.decision_latency().latency_ms < u32::MAX);
    }

    #[test]
    fn unblock_higher_priority_of_kill_request() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let mut kills_client_test: KillsClient =
            KillsClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        write_kill_decision_response(&mut server_vsock_test, 1, 100);
        let first_notification_timestamp = Instant::now();
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP,
                first_notification_timestamp,
            )
            .expect("Should be able to handle psi notification");
        let _kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        let _latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        write_kill_decision_response(&mut server_vsock_test, 2, 100);
        kills_client_test
            .handle_psi_notification(
                ResizePriority::RESIZE_PRIORITY_FOCUSED_APP,
                first_notification_timestamp,
            )
            .expect("Should be able to handle psi notification");

        let kill_request = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            kill_request.type_,
            PacketType::PACKET_TYPE_KILL_REQUEST.into()
        );
        assert_eq!(
            kill_request.kill_decision_request().priority,
            ResizePriority::RESIZE_PRIORITY_FOCUSED_APP.into()
        );
        let latency_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            latency_packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(latency_packet.decision_latency().sequence_num, 2);
        assert!(latency_packet.decision_latency().latency_ms < u32::MAX);
    }
}
