// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Result;
use protobuf::MessageField;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::MglruStats;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::VmMemoryManagementPacket;

use crate::vmmms_socket::VmmmsSocket;

pub struct VmmmsClient {
    vmmms_socket: VmmmsSocket,
}

impl VmmmsClient {
    pub fn new(mut vmmms_socket: VmmmsSocket) -> Result<Self> {
        let mut handshake_packet = VmMemoryManagementPacket::new();
        handshake_packet.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        handshake_packet.mut_handshake().type_ = ConnectionType::CONNECTION_TYPE_STATS.into();

        vmmms_socket.write_packet(handshake_packet)?;

        let handshake_response = vmmms_socket.read_packet()?;

        if handshake_response.type_ != PacketType::PACKET_TYPE_CONNECTION_ACK.into() {
            bail!("VmMemoryManagement Server did not ack");
        }

        Ok(Self {
            vmmms_socket: vmmms_socket,
        })
    }

    pub fn handle_reclaim_socket_readable(self: &mut Self) -> Result<()> {
        let mglru_request = self.vmmms_socket.read_packet()?;

        if mglru_request.type_ != PacketType::PACKET_TYPE_MGLRU_REQUEST.into() {
            bail!("Received unsupported command on MGLRU request.");
        };

        let mut mglru_packet = VmMemoryManagementPacket::new();
        mglru_packet.type_ = PacketType::PACKET_TYPE_MGLRU_RESPONSE.into();
        let stats: MessageField<MglruStats> = Default::default();
        mglru_packet.mut_mglru_response().stats = stats;

        self.vmmms_socket.write_packet(mglru_packet)?;

        Ok(())
    }
}
