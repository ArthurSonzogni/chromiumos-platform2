// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that handles the communication api for sending messages between
//! Dugong and Trichechus

use std::fmt::Debug;
use std::result::Result as StdResult;
use std::str::FromStr;

use anyhow::Context;
use log::info;
use serde::Deserialize;
use serde::Serialize;
use serde_bytes::ByteBuf;
use sirenia_rpc_macros::sirenia_rpc;
use thiserror::Error as ThisError;

use crate::app_info::AppManifest;
use crate::transport::Transport;
use crate::transport::TransportType;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize, ThisError)]
pub enum Error {
    #[error("App ID not found in the manifest")]
    InvalidAppId,
    #[error("Digest of TEE app executable is missing from the manifest")]
    DigestMissing,
    #[error("Digest of TEE app executable did not match value in manifest")]
    DigestMismatch,
    #[error("App not loadable")]
    AppNotLoadable,
    #[error("App requires developer mode")]
    RequiresDevmode,
    #[error("Sandbox type not implemented")]
    SandboxTypeNotImplemented,
    #[error("App not found at expected path")]
    AppPath,
    #[error("App not loaded yet")]
    AppNotLoaded,
    #[error("The same source port used more than once")]
    DuplicateSourcePort,
    #[error("{0}")]
    Custom(String),
}

impl From<String> for Error {
    fn from(s: String) -> Self {
        Error::Custom(s)
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AppInfo {
    pub app_id: String,
    pub port_numbers: Vec<u32>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum SystemEvent {
    Halt,
    PowerOff,
    Reboot,
}

impl FromStr for SystemEvent {
    type Err = String;

    fn from_str(event: &str) -> StdResult<SystemEvent, String> {
        Ok(match event {
            "halt" => SystemEvent::Halt,
            "poweroff" => SystemEvent::PowerOff,
            "reboot" => SystemEvent::Reboot,
            _ => return Err(format!("Failed to convert '{}' to an event.", event)),
        })
    }
}

#[sirenia_rpc(error = "Error")]
pub trait Trichechus<E> {
    fn start_session(&mut self, app_info: AppInfo, args: Vec<String>) -> StdResult<u32, E>;
    fn load_app(&mut self, app_id: String, elf: Vec<u8>) -> StdResult<(), E>;
    #[error()]
    fn get_apps(&mut self) -> StdResult<AppManifest, E>;
    #[error()]
    fn get_logs(&mut self) -> StdResult<Vec<ByteBuf>, E>;

    fn prepare_manatee_memory_service_socket(&mut self, port_number: u32) -> StdResult<(), E>;

    fn system_event(&mut self, event: SystemEvent) -> StdResult<(), E>;
}

const RPC_FAILURE_CONTEXT: &str = "start_session rpc failed";
pub fn start_application_helper<
    T: Trichechus<anyhow::Error>,
    F: FnOnce(&mut T, &AppInfo, &[String], Error) -> Result<(), anyhow::Error>,
>(
    trichechus_client: &mut T,
    transport_type: &TransportType,
    app_id: &str,
    args: Vec<String>,
    num_channels: usize,
    failure_callback: F,
) -> Result<Vec<Transport>, anyhow::Error> {
    let mut transports = Vec::with_capacity(num_channels);
    let mut ports = Vec::with_capacity(num_channels);
    for _ in 0..num_channels {
        let mut transport = transport_type
            .try_into_client(None)
            .context("failed to get client for transport")?;
        let addr = transport.bind().context("failed to bind to socket")?;
        ports.push(addr.get_port().context("failed to get port")?);
        transports.push(transport);
    }

    let app_info = AppInfo {
        app_id: String::from(app_id),
        port_numbers: ports,
    };
    info!("Requesting start '{:?}' {:?}", &app_info, &args);
    if let Err(err) = trichechus_client.start_session(app_info.clone(), args.clone()) {
        match err.downcast() {
            Ok(err) => failure_callback(trichechus_client, &app_info, &args, err)
                .context(RPC_FAILURE_CONTEXT)?,
            Err(err) => Err(err).context(RPC_FAILURE_CONTEXT)?,
        }
    }

    let mut connections = Vec::with_capacity(num_channels);
    for mut transport in transports {
        connections.push(transport.connect().context("failed to connect to socket")?);
    }
    Ok(connections)
}
