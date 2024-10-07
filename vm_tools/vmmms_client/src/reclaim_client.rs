// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Result;
use once_cell::sync::Lazy;
use std::fs::File;
use std::io::Read;
use std::io::Seek;
use std::io::SeekFrom::Start;
use std::path::Path;
use std::time::Duration;
use std::time::Instant;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::VmMemoryManagementPacket;

use crate::mglru::parse_mglru_stats;
use crate::vmmms_socket::VmmmsSocket;

static PAGE_SIZE: Lazy<usize> = Lazy::new(|| {
    // SAFETY: sysconf is memory safe.
    unsafe { libc::sysconf(libc::_SC_PAGE_SIZE) as usize }
});

pub fn get_page_size() -> usize {
    *PAGE_SIZE
}

pub struct ReclaimClient {
    pub vmmms_socket: VmmmsSocket,
    pub mglru_file: File,
    last_send_timestamp: Option<Instant>,
}

impl ReclaimClient {
    pub fn new(mut vmmms_socket: VmmmsSocket, mglru_file_path: &Path) -> Result<Self> {
        vmmms_socket.handshake(ConnectionType::CONNECTION_TYPE_STATS)?;

        let mglru_file = File::open(mglru_file_path)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
            mglru_file: mglru_file,
            last_send_timestamp: None,
        })
    }

    fn generate_mglru_stats_packet(self: &mut Self) -> Result<VmMemoryManagementPacket> {
        let mut mglru_file_buffer = Vec::new();
        self.mglru_file.seek(Start(0))?;
        self.mglru_file.read_to_end(&mut mglru_file_buffer)?;
        let raw_stats = String::from_utf8(mglru_file_buffer)?;

        let mut mglru_packet = VmMemoryManagementPacket::new();
        mglru_packet.type_ = PacketType::PACKET_TYPE_MGLRU_RESPONSE.into();
        mglru_packet.mut_mglru_response().stats =
            Some(parse_mglru_stats(&raw_stats, get_page_size())?).into();

        Ok(mglru_packet)
    }

    pub fn handle_reclaim_socket_readable(self: &mut Self) -> Result<()> {
        let mglru_request = self.vmmms_socket.read_packet()?;

        if mglru_request.type_ != PacketType::PACKET_TYPE_MGLRU_REQUEST.into() {
            bail!("Received unsupported command on MGLRU request.");
        };

        let mglru_response = self.generate_mglru_stats_packet()?;
        self.vmmms_socket.write_packet(mglru_response)?;
        Ok(())
    }

    pub fn handle_mglru_notification(self: &mut Self, current_time: Instant) -> Result<()> {
        const COOLDOWN: Duration = Duration::from_secs(1);
        if let Some(last_send_timestamp) = self.last_send_timestamp {
            if current_time < last_send_timestamp + COOLDOWN {
                let mut mglru_file_buffer = Vec::new();
                self.mglru_file.seek(Start(0))?;
                self.mglru_file.read_to_end(&mut mglru_file_buffer)?;
                return Ok(());
            }
        }

        self.last_send_timestamp = Some(current_time);
        let mglru_response = self.generate_mglru_stats_packet()?;
        self.vmmms_socket.write_packet(mglru_response)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::vmmms_socket::tests::write_handshake_response;
    use crate::ReclaimClient;
    use crate::VmmmsSocket;
    use crate::READ_TIMEOUT;
    use std::io::Write;
    use std::time::Instant;
    use system_api::vm_memory_management::ConnectionType;
    use system_api::vm_memory_management::PacketType;
    use system_api::vm_memory_management::VmMemoryManagementPacket;
    use tempfile::NamedTempFile;

    const SIMPLE_MGLRU_FILE: &str = r"memcg     1
    node     2
    3      4      5        6
";

    #[test]
    fn connection_succeeds() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);

        let file = NamedTempFile::new().unwrap();
        let _reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");

        let handshake_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            handshake_packet.type_,
            PacketType::PACKET_TYPE_HANDSHAKE.into()
        );
        assert_eq!(
            handshake_packet.handshake().type_,
            ConnectionType::CONNECTION_TYPE_STATS.into()
        );
    }

    #[test]
    fn handle_reclaim_socket_readable_succeeds() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let mut file = NamedTempFile::new().unwrap();
        let mut reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        file.write_all(SIMPLE_MGLRU_FILE.as_bytes()).unwrap();

        let mut mglru_request = VmMemoryManagementPacket::new();
        mglru_request.type_ = PacketType::PACKET_TYPE_MGLRU_REQUEST.into();
        server_vsock_test
            .write_packet(mglru_request)
            .expect("Should be able to write packet");
        reclaim_client_test
            .handle_reclaim_socket_readable()
            .unwrap();

        let mglru_response = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            mglru_response.type_,
            PacketType::PACKET_TYPE_MGLRU_RESPONSE.into()
        );
        assert!(mglru_response.has_mglru_response());
    }

    #[test]
    fn invalid_mglru_rerquest() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let file = NamedTempFile::new().unwrap();
        let mut reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        let mut invalid_mglru_request = VmMemoryManagementPacket::new();
        invalid_mglru_request.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        server_vsock_test
            .write_packet(invalid_mglru_request)
            .expect("Should be able to write packet");

        assert!(reclaim_client_test
            .handle_reclaim_socket_readable()
            .is_err());
    }

    #[test]
    fn handle_mglru_notification_succeeds() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let mut file = NamedTempFile::new().unwrap();
        let mut reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        file.write_all(SIMPLE_MGLRU_FILE.as_bytes()).unwrap();
        reclaim_client_test.handle_mglru_notification(Instant::now()).unwrap();

        let mglru_response = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            mglru_response.type_,
            PacketType::PACKET_TYPE_MGLRU_RESPONSE.into()
        );
        assert!(mglru_response.has_mglru_response());
    }

    #[test]
    fn empty_mglru_file() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let file = NamedTempFile::new().unwrap();
        let mut reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test.read_packet().unwrap();
        assert!(reclaim_client_test
            .handle_mglru_notification(Instant::now())
            .is_err());
    }

    #[test]
    fn block_successive_mglru_notification() {
        let (client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        write_handshake_response(&mut server_vsock_test);
        let mut file = NamedTempFile::new().unwrap();
        let mut reclaim_client_test: ReclaimClient =
            ReclaimClient::new(client_vsock_test, file.path()).expect("Should complete handshake");
        let _handshake_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        file.write_all(SIMPLE_MGLRU_FILE.as_bytes()).unwrap();
        reclaim_client_test
            .handle_mglru_notification(Instant::now())
            .unwrap();
        reclaim_client_test
            .handle_mglru_notification(Instant::now())
            .unwrap();

        let _mglru_response = server_vsock_test.read_packet().unwrap();
        assert!(server_vsock_test.read_packet().is_err());
    }
}
