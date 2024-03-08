// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::ffi::OsStr;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde::Deserialize;

use crate::config::program::RawProgram;
use crate::config::{config::RawConfig, Config};

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
/// Additional config files.
///
/// # Example config
/// ```toml
/// [programs]
/// arc = { file_name="arc.log", severity="4", match_name="arc-.*" }
/// arcvm = { file_name="arc.log", severity="4", match_name="arcvm.*" }
/// ```
struct AdditionalConfig {
    programs: HashMap<Box<str>, RawProgram>,
}

#[cfg(feature = "chromeos")]
fn get_config_path() -> PathBuf {
    ["/", "etc", "soul"].iter().collect()
}

#[cfg(not(feature = "chromeos"))]
fn get_config_path() -> PathBuf {
    [env!("CARGO_MANIFEST_DIR"), "tests", "config_files", "soul"]
        .iter()
        .collect()
}

fn get_config_dir() -> PathBuf {
    get_config_path().with_extension("d")
}

fn find_additional_config_files() -> Vec<PathBuf> {
    let dir = get_config_dir();

    match std::fs::read_dir(&dir) {
        Ok(iter) => iter
            .filter_map(|entry| entry.ok())
            .filter(|path| path.path().is_file())
            .filter(|file| file.path().extension().unwrap_or(OsStr::new("")) == "toml")
            .map(|file| PathBuf::from(file.path()))
            .collect(),
        Err(err) => {
            log::warn!("Failed to get dir listing for {}: {err}", dir.display());
            return Vec::new();
        }
    }
}

fn read_config<T: serde::de::DeserializeOwned>(config_file: &Path) -> Result<T> {
    let mut file = File::open(config_file)?;
    let size: usize = match file.metadata() {
        Ok(meta) => meta.len() as usize,
        Err(err) => {
            log::debug!("Couldn't get file metadata: {err}");
            0
        }
    };
    let mut buf = String::with_capacity(size);

    let _ = file
        .read_to_string(&mut buf)
        .with_context(|| format!("Reading config file '{}'", config_file.display()))?;
    log::trace!(
        "Attempting to deserialize {} as {}",
        config_file.display(),
        std::any::type_name::<T>()
    );
    let cfg: T = toml::from_str(&buf)
        .with_context(|| format!("Parsing config file '{}'", config_file.display()))?;
    Ok(cfg)
}

/// Read and build the system config.
///
/// This function reads the main config file and finds all supplemental config
/// files, combines them and returns the whole configuration.
pub fn read() -> Result<Config> {
    log::trace!("Reading main config file");
    let main_config_file = get_config_path().with_extension("toml");
    let mut main_config = read_config::<RawConfig>(&main_config_file)?;

    for additional_file in find_additional_config_files() {
        log::trace!("Reading supplemental config: {}", additional_file.display());
        let additional_config = read_config::<AdditionalConfig>(&additional_file)?;
        for (name, program) in additional_config.programs {
            main_config.set_program(name, program);
        }
    }

    Ok(main_config.eager())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn get_main_file() -> PathBuf {
        get_config_path().with_extension("toml")
    }

    #[test]
    fn parse_main_as_main() {
        let main_file = get_main_file();

        assert!(read_config::<RawConfig>(&main_file).is_ok());
    }

    #[test]
    fn parse_main_as_supplemental() {
        let main_file = get_main_file();

        assert!(read_config::<AdditionalConfig>(&main_file)
            .unwrap_err()
            .is::<toml::de::Error>());
    }

    #[test]
    fn parse_supplemental_as_supplemenatal() {
        let files = find_additional_config_files();
        assert_ne!(files.len(), 0);
        for file in files {
            if file
                .file_name()
                .unwrap()
                .to_string_lossy()
                .starts_with("invalid_")
            {
                continue;
            }
            assert!(read_config::<AdditionalConfig>(&file).is_ok());
        }
    }

    #[test]
    fn parse_invalid_supplemental_as_supplemenatal() {
        let files = find_additional_config_files();
        assert_ne!(files.len(), 0);
        for file in files {
            if !file
                .file_name()
                .unwrap()
                .to_string_lossy()
                .starts_with("invalid_")
            {
                continue;
            }
            assert!(read_config::<AdditionalConfig>(&file).is_err());
        }
    }

    #[test]
    fn parse_supplemental_as_main() {
        let files = find_additional_config_files();
        assert_ne!(files.len(), 0);
        for file in files {
            assert!(read_config::<RawConfig>(&file)
                .unwrap_err()
                .is::<toml::de::Error>());
        }
    }

    #[test]
    fn read_non_existing_file() {
        let file = get_config_path().with_extension("DOESN'T EXIST");

        assert!(read_config::<RawConfig>(&file)
            .unwrap_err()
            .is::<std::io::Error>());
        assert!(read_config::<AdditionalConfig>(&file)
            .unwrap_err()
            .is::<std::io::Error>());
    }
}
