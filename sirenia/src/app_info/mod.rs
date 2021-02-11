// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that defines the app_manifest.
use std::collections::HashMap;
use std::fmt::Debug;
use std::result::Result as StdResult;

use libsirenia::communication::persistence::Scope;
use serde::{Deserialize, Serialize};
use thiserror::Error as ThisError;

#[derive(ThisError, Debug)]
pub enum Error {
    /// Invalid app id.
    #[error("invalid app id: {0}")]
    InvalidAppId(String),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

pub struct AppManifest {
    entries: HashMap<String, AppManifestEntry>,
}

/// Defines the type of isolation given to the TEE app instance.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum SandboxType {
    Container,
    DeveloperEnvironment,
    VirtualMachine,
}

/// The TEE developer will define all of these fields for their app and the
/// manifest will be used when starting up the TEE app.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct AppManifestEntry {
    pub app_name: String,
    pub scope: Scope,
    pub path: String,
    pub domain: String,
    pub sandbox_type: SandboxType,
}

/// Provides a lookup for registered AppManifestEntries that represent which
/// TEE apps are recognized.
impl AppManifest {
    pub fn new() -> Self {
        let mut manifest = AppManifest {
            entries: HashMap::new(),
        };
        // TODO: Manifests are hardcoded here for now while we determine the
        // best way to store manifests.
        manifest.add_app_manifest_entry(AppManifestEntry {
            app_name: "shell".to_string(),
            scope: Scope::Test,
            path: "/bin/sh".to_string(),
            domain: "test".to_string(),
            sandbox_type: SandboxType::DeveloperEnvironment,
        });
        manifest.add_app_manifest_entry(AppManifestEntry {
            app_name: "sandboxed-shell".to_string(),
            scope: Scope::Test,
            path: "/bin/sh".to_string(),
            domain: "test".to_string(),
            sandbox_type: SandboxType::Container,
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
}

impl Default for AppManifest {
    fn default() -> Self {
        AppManifest::new()
    }
}
