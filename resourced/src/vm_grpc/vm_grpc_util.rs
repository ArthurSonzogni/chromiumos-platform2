// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use dbus::message::Message;
use glob::glob;
use std::path::Path;
use std::process::Command;

use crate::vm_grpc::vm_grpc_client::VmGrpcClient;
use crate::vm_grpc::vm_grpc_server::VmGrpcServer;
use libchromeos::sys::info;
use std::sync::atomic::AtomicI64;

// VSOCK port to use for accepting GRPC socket connections.
const RESOURCED_GRPC_SERVER_PORT: u16 = 5551;

// VSOCK port where resourced can connect a client.
const GUEST_VM_GRPC_SERVER_PORT: u16 = 5553;

// VSOCK CID for accepting any serverside connection.
const CID_ANY: i16 = -1;

pub(crate) fn vm_grpc_init(msg: &Message) -> Result<()> {
    use std::sync::Arc;

    info!("VmStartedSignal RX => {:?}", msg);

    // TODO (b/266499318): remove this when dbus payload decoding is available
    // resourced run as a daemon should not call an executable.  Use dbus/IPC
    // calls instead for cryptohome and CID.
    let borealis_cid = get_vm_cid("borealis")?;
    info!("Borealis CID: {borealis_cid}");

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

// TODO (b/266499318): Remove this once dbus message decoding is implemented.
pub(crate) fn get_vm_cid(vm_name: &str) -> Result<i16> {
    let crypto_home = get_crypto_home()?;

    // Blocking call.  concierge_client fails fast.
    let concierge_out = Command::new("/usr/bin/concierge_client")
        .arg("--get_vm_cid")
        .arg(format!("--cryptohome_id={crypto_home}"))
        .arg(format!("--name={}", vm_name))
        .output()?;

    Ok(String::from_utf8_lossy(&concierge_out.stdout)
        .to_string()
        .strip_suffix('\n')
        .unwrap_or_default()
        .parse::<i16>()?)
}

// TODO (b/266499318): Remove this once dbus message decoding is implemented.
pub(crate) fn get_crypto_home() -> Result<String> {
    let crypto_home: String;
    let mut home_hashes = glob("/home/root/*")?;

    // Only getting the first path match here, which should be the primary user.
    if let Some(home_hash) = home_hashes.next() {
        crypto_home = home_hash?
            .file_name()
            .and_then(|x| x.to_str())
            .unwrap_or_default()
            .to_string();
        info!("cryptohome: {:?}", crypto_home);
    } else {
        bail!("Could not read cryptohome");
    }

    Ok(crypto_home)
}
