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
