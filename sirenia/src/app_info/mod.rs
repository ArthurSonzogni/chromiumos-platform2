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
// The tee developer will define all of these fields for their app and the
// manifest will be used when starting up the TEE app.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct AppManifestEntry {
    pub app_name: String,
    pub scope: Scope,
    pub path: String,
    pub domain: String,
}

// TODO: Manifests are hardcoded here for now while we determine the best way
// to store manifests.
impl AppManifest {
    pub fn new() -> Self {
        let mut entries = HashMap::new();
        let shell = AppManifestEntry {
            app_name: "shell".to_string(),
            scope: Scope::Test,
            path: "/bin/sh".to_string(),
            domain: "test".to_string(),
        };
        entries.insert("shell".to_string(), shell);
        AppManifest { entries }
    }

    pub fn get_app_manifest_entry(&self, id: &str) -> Result<&AppManifestEntry> {
        match self.entries.get(id) {
            Some(entry) => Ok(entry),
            None => Err(Error::InvalidAppId(id.to_string())),
        }
    }
}
