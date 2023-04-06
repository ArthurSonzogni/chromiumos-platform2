// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::vm_grpc::vm_grpc_client::VmGrpcClient;
use crate::vm_grpc::vm_grpc_server::VmGrpcServer;
use anyhow::{bail, Result};
use dbus::message::Message;
use protobuf::CodedInputStream;
use protobuf::Message as protoMessage;
use std::path::Path;
use std::sync::atomic::AtomicI64;
mod arc;
mod concierge_service;

// VSOCK port to use for accepting GRPC socket connections.
const RESOURCED_GRPC_SERVER_PORT: u16 = 5551;

// VSOCK port where resourced can connect a client.
const GUEST_VM_GRPC_SERVER_PORT: u16 = 5553;

// VSOCK CID for accepting any serverside connection.
const CID_ANY: i16 = -1;

// Name for the borealis VM.
const BOREALIS_VM_NAME: &str = "borealis";

// Extracts the CID for the borealis VM from the D-bus message.
fn get_borealis_cid(msg: &Message) -> Result<i16> {
    let raw_buffer: Vec<u8> = msg.read1()?;
    let input = &mut CodedInputStream::from_bytes(&raw_buffer);
    let mut borealis_vm = concierge_service::VmStartedSignal::new();
    borealis_vm.merge_from(input)?;
    let name_vm = borealis_vm.get_name();
    if name_vm != BOREALIS_VM_NAME {
        bail!("ignoring VmStartedSIgnal for {}.", name_vm)
    }
    let borealis_cid = borealis_vm.get_vm_info().get_cid() as i16;
    Ok(borealis_cid)
}

pub(crate) fn vm_grpc_init(msg: &Message) -> Result<()> {
    use std::sync::Arc;
    let borealis_cid = get_borealis_cid(msg)?;
    let root = Path::new("/");
    let pkt_tx_interval = Arc::new(AtomicI64::new(-1));
    let pkt_tx_interval_clone = pkt_tx_interval.clone();

    let _server = VmGrpcServer::run(CID_ANY, RESOURCED_GRPC_SERVER_PORT, root, pkt_tx_interval)?;
    VmGrpcClient::run(
        borealis_cid,
        GUEST_VM_GRPC_SERVER_PORT,
        root,
        pkt_tx_interval_clone,
    )?;

    Ok(())
}
