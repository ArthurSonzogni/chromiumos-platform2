// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that handles the communication api for sending messages between
//! Dugong and Trichechus

use std::fmt::Debug;
use std::result::Result as StdResult;
use std::str::FromStr;

use serde::{Deserialize, Serialize};
use sirenia_rpc_macros::sirenia_rpc;
use thiserror::Error as ThisError;

use crate::communication::ExecutableInfo;

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
    #[error("Sandbox type not implemented")]
    SandboxTypeNotImplemented,
    #[error("App not found at expected path")]
    AppPath,
    #[error("App not loaded yet")]
    AppNotLoaded,
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
    pub port_number: u32,
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

#[sirenia_rpc]
pub trait Trichechus {
    type Error;

    fn start_session(&self, app_info: AppInfo) -> StdResult<Result<(), Error>, Self::Error>;
    fn load_app(&self, app_id: String, elf: Vec<u8>) -> StdResult<Result<(), Error>, Self::Error>;
    fn get_apps(&self) -> StdResult<Vec<(String, ExecutableInfo)>, Self::Error>;
    fn get_logs(&self) -> StdResult<Vec<Vec<u8>>, Self::Error>;

    fn system_event(&self, event: SystemEvent) -> StdResult<Result<(), String>, Self::Error>;
}
