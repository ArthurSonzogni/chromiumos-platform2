// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Result;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::time::Duration;
use system_api::vm_memory_management::ConnectionType;

use crate::vmmms_socket::VmmmsSocket;

pub struct KillsClient {
    pub vmmms_socket: VmmmsSocket,
    pub psi_file: File,
}

impl KillsClient {
    pub fn new(mut vmmms_socket: VmmmsSocket, psi_file_path: &str) -> Result<Self> {
        vmmms_socket.handshake(ConnectionType::CONNECTION_TYPE_KILLS)?;

        const STALL_DURATION: std::time::Duration = Duration::from_millis(100);
        let psi_file = Self::set_trigger(psi_file_path, STALL_DURATION)?;

        Ok(Self {
            vmmms_socket: vmmms_socket,
            psi_file: psi_file,
        })
    }

    fn set_trigger(psi_file_path: &str, stall: std::time::Duration) -> Result<File> {
        let mut psi_file: File = OpenOptions::new()
            .read(true)
            .write(true)
            .open(psi_file_path)?;

        const TARGET: &str = "some";
        // The window size for monitors made by unprivileged users
        // is restricted to multiples of 2s
        const WINDOW_DURATION: std::time::Duration = Duration::from_secs(2);

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
}
