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
}

impl ReclaimClient {
    pub fn new(mut vmmms_socket: VmmmsSocket, mglru_file_path: &str) -> Result<Self> {
        let mut handshake_packet = VmMemoryManagementPacket::new();
        handshake_packet.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        handshake_packet.mut_handshake().type_ = ConnectionType::CONNECTION_TYPE_STATS.into();

        vmmms_socket.write_packet(handshake_packet)?;

        let handshake_response = vmmms_socket.read_packet()?;

        if handshake_response.type_ != PacketType::PACKET_TYPE_CONNECTION_ACK.into() {
            bail!("VmMemoryManagement Server did not ack");
        }

        let mglru_file = File::open(mglru_file_path)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
            mglru_file: mglru_file,
        })
    }

    pub fn generate_mglru_stats_packet(self: &mut Self) -> Result<VmMemoryManagementPacket> {
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

    pub fn handle_mglru_notification(self: &mut Self) -> Result<()> {
        let mglru_response = self.generate_mglru_stats_packet()?;
        self.vmmms_socket.write_packet(mglru_response)?;
        Ok(())
    }
}
