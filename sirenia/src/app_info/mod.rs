// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that defines the app_manifest.
use std::collections::BTreeMap as Map;
use std::fmt::Debug;
use std::fs::{read_dir, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::result::Result as StdResult;

use libsirenia::communication::persistence::Scope;
use serde::{Deserialize, Serialize};
use thiserror::Error as ThisError;

use crate::Digest;

pub const DEFAULT_APP_CONFIG_PATH: &str = "/usr/share/manatee";
pub const JSON_EXTENSION: &str = ".json";
pub const FLEXBUFFER_EXTENSION: &str = ".flex.bin";

#[derive(ThisError, Debug)]
pub enum Error {
    /// Invalid app id.
    #[error("invalid app id: {0}")]
    InvalidAppId(String),
    #[error("failed to open config path: {0}")]
    OpenConfig(io::Error),
    #[error("failed to open config dir: {0}")]
    OpenConfigDir(io::Error),
    #[error("failed to read config: {0}")]
    ReadConfig(io::Error),
    #[error("failed to read config dir: {0}")]
    ReadConfigDir(io::Error),
    #[error("failed to parse flexbuffer config: {0}")]
    FlexbufferParse(flexbuffers::DeserializationError),
    #[error("failed to parse json config: {0}")]
    JsonParse(serde_json::Error),
    #[error("invalid config name: {0:?}")]
    InvalidConfigName(PathBuf),
    #[error("failed to parse json config: {0:?}")]
    UnknownConfigFormat(PathBuf),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

pub struct AppManifest {
    entries: Map<String, AppManifestEntry>,
}

/// Defines the type of isolation given to the TEE app instance.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub enum SandboxType {
    Container,
    DeveloperEnvironment,
    VirtualMachine,
}

/// Defines parameters for use with the storage API.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct StorageParameters {
    pub scope: Scope,
    pub domain: String,
    /// Enables transparent storage encryption.
    pub encryption_key_version: Option<usize>,
}

/// Defines parameters for use with the secrets API.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct SecretsParameters {
    pub encryption_key_version: usize,
}

/// The TEE developer will define all of these fields for their app and the
/// manifest will be used when starting up the TEE app.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct AppManifestEntry {
    pub app_name: String,
    pub exec_info: ExecutableInfo,
    pub sandbox_type: SandboxType,
    pub secrets_parameters: Option<SecretsParameters>,
    pub storage_parameters: Option<StorageParameters>,
}

#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub enum ExecutableInfo {
    Path(String),
    Digest(Digest),
}

/// Provides a lookup for registered AppManifestEntries that represent which
/// TEE apps are recognized.
impl AppManifest {
    pub fn new() -> Self {
        // TODO drop vec! after Rust 1.53 which has an IntoIterator implementation for arrays.
        AppManifest::from(vec![
            AppManifestEntry {
                app_name: "shell".to_string(),
                exec_info: ExecutableInfo::Path("/bin/sh".to_string()),
                sandbox_type: SandboxType::DeveloperEnvironment,
                secrets_parameters: None,
                storage_parameters: None,
            },
            AppManifestEntry {
                app_name: "sandboxed-shell".to_string(),
                exec_info: ExecutableInfo::Path("/bin/sh".to_string()),
                sandbox_type: SandboxType::Container,
                secrets_parameters: None,
                storage_parameters: None,
            },
            AppManifestEntry {
                app_name: "demo_app".to_string(),
                exec_info: ExecutableInfo::Path("/usr/bin/demo_app".to_string()),
                sandbox_type: SandboxType::DeveloperEnvironment,
                secrets_parameters: None,
                storage_parameters: Some(StorageParameters {
                    scope: Scope::Test,
                    domain: "test".to_string(),
                    encryption_key_version: Some(1),
                }),
            },
        ])
    }

    pub fn load_default() -> Result<Self> {
        let mut manifest = AppManifest::new();

        let default_path = Path::new(DEFAULT_APP_CONFIG_PATH);
        if !default_path.exists() {
            return Ok(manifest);
        }

        for entries in entries_from_path(default_path)? {
            manifest.add_app_manifest_entry(entries);
        }
        Ok(manifest)
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

    pub fn append<I: IntoIterator<Item = AppManifestEntry>>(&mut self, entries: I) {
        for entry in entries.into_iter() {
            self.add_app_manifest_entry(entry);
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = &AppManifestEntry> {
        self.entries.iter().map(|kv| kv.1)
    }
}

impl<I: IntoIterator<Item = AppManifestEntry>> From<I> for AppManifest {
    fn from(entries: I) -> Self {
        let mut manifest = AppManifest {
            entries: Map::new(),
        };
        manifest.append(entries);
        manifest
    }
}

impl Default for AppManifest {
    fn default() -> Self {
        AppManifest::new()
    }
}

/// Parse AppManifestEntries from configuration(s) at the path. This accepts both files and
/// directories.
pub fn entries_from_path<A: AsRef<Path>>(name: A) -> Result<Vec<AppManifestEntry>> {
    if name.as_ref().is_dir() {
        let mut entries = Vec::<AppManifestEntry>::new();
        for entry in read_dir(name).map_err(Error::OpenConfigDir)? {
            let entry_path = entry.map_err(Error::ReadConfigDir)?.path();
            if !entry_path.is_file() {
                continue;
            }
            entries.append(&mut entries_from_path(entry_path)?);
        }
        Ok(entries)
    } else {
        let lowercase_name = name
            .as_ref()
            .to_str()
            .ok_or_else(|| Error::InvalidConfigName(name.as_ref().to_path_buf()))?
            .to_lowercase();
        let mut file = File::open(name.as_ref()).map_err(Error::OpenConfig)?;
        let entries: Vec<AppManifestEntry> = if lowercase_name.ends_with(JSON_EXTENSION) {
            serde_json::from_reader(file).map_err(Error::JsonParse)?
        } else if lowercase_name.ends_with(FLEXBUFFER_EXTENSION) {
            let mut contents = Vec::<u8>::new();
            file.read_to_end(&mut contents).map_err(Error::ReadConfig)?;
            flexbuffers::from_slice(&contents).map_err(Error::FlexbufferParse)?
        } else {
            return Err(Error::UnknownConfigFormat(name.as_ref().to_path_buf()));
        };
        Ok(entries)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    const TEST_MANIFEST_ENTRY: &str = r#"{
  "app_name": "demo_app",
  "exec_info": {
    "Path": "/usr/bin/demo_app"
  },
  "sandbox_type": "DeveloperEnvironment",
  "secrets_parameters": null,
  "storage_parameters": {
    "scope": "Test",
    "domain": "test",
    "encryption_key_version": 1
  }
}"#;

    #[test]
    fn check_serialization() {
        let manifest = AppManifest::default();
        let test_entry = serde_json::from_str::<'_, AppManifestEntry>(TEST_MANIFEST_ENTRY).unwrap();
        let expected_entry = manifest
            .get_app_manifest_entry(&test_entry.app_name)
            .unwrap();
        assert_eq!(&test_entry, expected_entry);
    }
}
