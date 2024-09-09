// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use system_api::vm_memory_management::ConnectionType;

use crate::vmmms_socket::VmmmsSocket;

pub struct KillsClient {
    pub vmmms_socket: VmmmsSocket,
}

impl KillsClient {
    pub fn new(mut vmmms_socket: VmmmsSocket) -> Result<Self> {
        vmmms_socket.handshake(ConnectionType::CONNECTION_TYPE_KILLS)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
        })
    }
}
