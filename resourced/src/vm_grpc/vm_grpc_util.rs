// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use dbus::message::Message;
use std::path::Path;

use crate::vm_grpc::vm_grpc_client::VmGrpcClient;
use crate::vm_grpc::vm_grpc_server::VmGrpcServer;
use libchromeos::sys::info;
use std::sync::atomic::AtomicI64;

// VSOCK port to use for accepting GRPC socket connections.
const RESOURCED_GRPC_SERVER_PORT: u16 = 5551;

// VSOCK port where resourced can connect a client.
const GUEST_VM_GRPC_SERVER_PORT: u16 = 5553;

// Default CID to use for borealis.
// TODO: remove compile-time hardcode and retrieve from dbus info packet.
const DEFAULT_GUEST_VM_CID: i16 = 34;

// VSOCK CID for accepting any serverside connection.
const CID_ANY: i16 = -1;

#[cfg(feature = "vm_grpc")]
pub(crate) fn vm_grpc_init(msg: &Message) -> Result<()> {
    use std::sync::Arc;

    info!("VmStartedSignal RX => {:?}", msg);
    // TODO: parse msg and retrieve CID. (using hardcoded default temporarily).

    let root = Path::new("/");
    let pkt_tx_interval = Arc::new(AtomicI64::new(-1));
    let pkt_tx_interval_clone = pkt_tx_interval.clone();

    let _server = VmGrpcServer::run(CID_ANY, RESOURCED_GRPC_SERVER_PORT, root, pkt_tx_interval)?;
    VmGrpcClient::run(
        DEFAULT_GUEST_VM_CID,
        GUEST_VM_GRPC_SERVER_PORT,
        root,
        pkt_tx_interval_clone,
    )?;

    Ok(())
}
