// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that defines the app_manifest.
use std::collections::BTreeMap as Map;
use std::fmt::Debug;
use std::result::Result as StdResult;

use libsirenia::communication::persistence::Scope;
use serde::{Deserialize, Serialize};
use thiserror::Error as ThisError;

use crate::Digest;

#[derive(ThisError, Debug)]
pub enum Error {
    /// Invalid app id.
    #[error("invalid app id: {0}")]
    InvalidAppId(String),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

pub struct AppManifest {
    entries: Map<String, AppManifestEntry>,
}

/// Defines the type of isolation given to the TEE app instance.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum SandboxType {
    Container,
    DeveloperEnvironment,
    VirtualMachine,
}

/// Defines parameters for use with the storage API.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct StorageParameters {
    pub scope: Scope,
    pub domain: String,
    /// Enables transparent storage encryption.
    pub encryption_key_version: Option<usize>,
}

/// Defines parameters for use with the secrets API.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct SecretsParameters {
    pub encryption_key_version: usize,
}

/// The TEE developer will define all of these fields for their app and the
/// manifest will be used when starting up the TEE app.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct AppManifestEntry {
    pub app_name: String,
    pub exec_info: ExececutableInfo,
    pub sandbox_type: SandboxType,
    pub secrets_parameters: Option<SecretsParameters>,
    pub storage_parameters: Option<StorageParameters>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum ExececutableInfo {
    Path(String),
    Digest(Digest),
}

/// Provides a lookup for registered AppManifestEntries that represent which
/// TEE apps are recognized.
impl AppManifest {
    pub fn new() -> Self {
        let mut manifest = AppManifest {
            entries: Map::new(),
        };
        manifest.add_app_manifest_entry(AppManifestEntry {
            app_name: "shell".to_string(),
            exec_info: ExececutableInfo::Path("/bin/sh".to_string()),
            sandbox_type: SandboxType::DeveloperEnvironment,
            secrets_parameters: None,
            storage_parameters: None,
        });
        manifest.add_app_manifest_entry(AppManifestEntry {
            app_name: "sandboxed-shell".to_string(),
            exec_info: ExececutableInfo::Path("/bin/sh".to_string()),
            sandbox_type: SandboxType::Container,
            secrets_parameters: None,
            storage_parameters: None,
        });
        manifest.add_app_manifest_entry(AppManifestEntry {
            app_name: "demo_app".to_string(),
            exec_info: ExececutableInfo::Path("/usr/bin/demo_app".to_string()),
            sandbox_type: SandboxType::DeveloperEnvironment,
            secrets_parameters: None,
            storage_parameters: Some(StorageParameters {
                scope: Scope::Test,
                domain: "test".to_string(),
                encryption_key_version: Some(1),
            }),
        });
        manifest
    }

    fn add_app_manifest_entry(&mut self, entry: AppManifestEntry) -> Option<AppManifestEntry> {
        let id = entry.app_name.clone();
        self.entries.insert(id, entry)
    }

    pub fn get_app_manifest_entry(&self, id: &str) -> Result<&AppManifestEntry> {
        match self.entries.get(id) {
            Some(entry) => Ok(entry),
            None => Err(Error::InvalidAppId(id.to_string())),
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = &AppManifestEntry> {
        self.entries.iter().map(|kv| kv.1)
    }
}

impl Default for AppManifest {
    fn default() -> Self {
        AppManifest::new()
    }
}
