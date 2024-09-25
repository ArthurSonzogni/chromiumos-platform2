// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use protobuf::Message;
use std::io::Read;
use std::io::Write;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use std::os::fd::FromRawFd;
use std::time::Duration;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::VmMemoryManagementPacket;
use vsock::VsockStream;

// VmmmsSocket is for reading and writing the packet to the server (concierge)
pub struct VmmmsSocket {
    socket: VsockStream,
}

impl VmmmsSocket {
    pub fn new(cid: u32, port: u32, timeout: Option<Duration>) -> Result<Self> {
        let socket = VsockStream::connect_with_cid_port(cid, port)
            .context("Failed to establish the socket connection")?;
        socket
            .set_read_timeout(timeout)
            .context("Failed to set read timeout")?;
        Ok(Self { socket: socket })
    }

    #[cfg(test)]
    pub fn pair_for_testing(timeout: Option<Duration>) -> (Self, Self) {
        // This code is quoted from crosvm base/sys/linux/mod.rs
        let mut pipe_fds = [-1; 2];
        // SAFETY:
        // Safe because socketpair will only write 2 element array of i32 to the given pointer,
        // and we check for error.
        let ret =
            unsafe { libc::socketpair(libc::AF_UNIX, libc::SOCK_STREAM, 0, &mut pipe_fds[0]) };
        assert_eq!(ret, 0);
        // SAFETY:
        // Safe because both fds must be valid for socketpair to have returned successfully
        // and we have exclusive ownership of them.
        let (client_vsock_test, server_vsock_test) = unsafe {
            (
                VsockStream::from_raw_fd(pipe_fds[0]),
                VsockStream::from_raw_fd(pipe_fds[1]),
            )
        };
        client_vsock_test
            .set_read_timeout(timeout)
            .expect("Failed to set read timeout");

        (
            VmmmsSocket {
                socket: client_vsock_test,
            },
            VmmmsSocket {
                socket: server_vsock_test,
            },
        )
    }

    pub fn write_packet(&mut self, packet: VmMemoryManagementPacket) -> Result<()> {
        let packet_in_bytes = packet
            .write_to_bytes()
            .context("Failed to serialize the packet")?;

        let packet_buffer = [
            &(packet_in_bytes.len() as u32).to_le_bytes()[..],
            &packet_in_bytes,
        ]
        .concat();

        self.socket
            .write_all(&packet_buffer)
            .context("Failed to write the packet")?;

        Ok(())
    }

    pub fn read_packet(&mut self) -> Result<VmMemoryManagementPacket> {
        let mut packet_size_in_bytes: [u8; 4] = [0; 4];
        self.socket
            .read_exact(&mut packet_size_in_bytes)
            .context("Failed to read the packet")?;

        let packet_size = u32::from_le_bytes(packet_size_in_bytes) as usize;

        // if the total packet size exceeds 64KB, there must be some mistake.
        const MAX_PACKET_SIZE: usize = 64 * 1024 - 4;
        if packet_size > MAX_PACKET_SIZE {
            bail!("Received packet is too large");
        }

        let mut packet_in_bytes: Vec<u8> = vec![0; packet_size];
        self.socket
            .read_exact(&mut packet_in_bytes)
            .context("Failed to read the packet")?;

        let received_packet: VmMemoryManagementPacket =
            VmMemoryManagementPacket::parse_from_bytes(&packet_in_bytes)
                .context("Failed to deserialize the packet")?;

        Ok(received_packet)
    }

    pub fn handshake(&mut self, connection_type: ConnectionType) -> Result<()> {
        let mut handshake_packet = VmMemoryManagementPacket::new();
        handshake_packet.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        handshake_packet.mut_handshake().type_ = connection_type.into();

        self.write_packet(handshake_packet)?;

        let handshake_response = self.read_packet()?;

        if handshake_response.type_ != PacketType::PACKET_TYPE_CONNECTION_ACK.into() {
            bail!("VmMemoryManagement Server did not ack");
        }
        Ok(())
    }
}

impl AsFd for VmmmsSocket {
    fn as_fd(&self) -> BorrowedFd<'_> {
        return self.socket.as_fd();
    }
}

#[cfg(test)]
mod tests {
    use crate::VmmmsSocket;
    use crate::READ_TIMEOUT;
    use protobuf::Message;
    use std::io::Write;
    use system_api::vm_memory_management::ConnectionType;
    use system_api::vm_memory_management::PacketType;
    use system_api::vm_memory_management::VmMemoryManagementPacket;
    #[test]
    fn write_and_read_packet_succeeds() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let mut handshake_packet = VmMemoryManagementPacket::new();
        handshake_packet.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        handshake_packet.mut_handshake().type_ = ConnectionType::CONNECTION_TYPE_STATS.into();
        client_vsock_test
            .write_packet(handshake_packet.clone())
            .expect("Shoule be able to write the packet");

        assert_eq!(
            server_vsock_test
                .read_packet()
                .expect("Should be able to read packet"),
            handshake_packet
        );
    }

    #[test]
    fn nothing_to_read() {
        let (mut client_vsock_test, _server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);

        assert!(client_vsock_test.read_packet().is_err());
    }

    #[test]
    fn fails_to_read_broken_packet_size() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let packet_in_bytes = VmMemoryManagementPacket::new()
            .write_to_bytes()
            .expect("Should be able to serialize the packet");
        // packet_buffer below is broken because its packet size is missing the first u8 item
        let packet_buffer = [
            &(packet_in_bytes.len() as u32).to_le_bytes()[1..],
            &packet_in_bytes,
        ]
        .concat();
        server_vsock_test
            .socket
            .write_all(&packet_buffer)
            .expect("Should be able to write packet");

        assert!(client_vsock_test.read_packet().is_err());
    }

    #[test]
    fn fails_to_read_broken_packet() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let mut handshake_response = VmMemoryManagementPacket::new();
        handshake_response.type_ = PacketType::PACKET_TYPE_CONNECTION_NACK.into();
        let packet_in_bytes = handshake_response
            .write_to_bytes()
            .expect("Should be able to serialize the packet");
        // packet_buffer below is broken because it is missing the last u8 item
        let packet_buffer = [
            &(packet_in_bytes.len() as u32).to_le_bytes()[..],
            &packet_in_bytes[..packet_in_bytes.len() - 1],
        ]
        .concat();
        server_vsock_test
            .socket
            .write_all(&packet_buffer)
            .expect("Should be able to write packet");

        assert!(client_vsock_test.read_packet().is_err());
    }

    #[test]
    fn handshake_response_does_not_exist() {
        let (mut client_vsock_test, _server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);

        assert!(client_vsock_test
            .handshake(ConnectionType::CONNECTION_TYPE_STATS)
            .is_err());
    }

    #[test]
    fn invalid_handshake_response() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let invalid_handshake_response = VmMemoryManagementPacket::new();
        server_vsock_test
            .write_packet(invalid_handshake_response)
            .expect("Should be able to write packet");

        assert!(client_vsock_test
            .handshake(ConnectionType::CONNECTION_TYPE_STATS)
            .is_err());
    }

    #[test]
    fn handshake_response_with_connection_nack() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let mut handshake_response = VmMemoryManagementPacket::new();
        handshake_response.type_ = PacketType::PACKET_TYPE_CONNECTION_NACK.into();
        server_vsock_test
            .write_packet(handshake_response)
            .expect("Should be able to write packet");

        assert!(client_vsock_test
            .handshake(ConnectionType::CONNECTION_TYPE_STATS)
            .is_err());
    }

    #[test]
    fn handshake_succeeds() {
        let (mut client_vsock_test, mut server_vsock_test) =
            VmmmsSocket::pair_for_testing(READ_TIMEOUT);
        let mut handshake_response = VmMemoryManagementPacket::new();
        handshake_response.type_ = PacketType::PACKET_TYPE_CONNECTION_ACK.into();
        server_vsock_test
            .write_packet(handshake_response)
            .expect("Should be able to write packet");

        assert!(client_vsock_test
            .handshake(ConnectionType::CONNECTION_TYPE_STATS)
            .is_ok());

        let mut handshake_packet = server_vsock_test
            .read_packet()
            .expect("Should be able to read packet");
        assert_eq!(
            handshake_packet.type_,
            PacketType::PACKET_TYPE_HANDSHAKE.into()
        );
        assert_eq!(
            handshake_packet.mut_handshake().type_,
            ConnectionType::CONNECTION_TYPE_STATS.into()
        );
    }
}
