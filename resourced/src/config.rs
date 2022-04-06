// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{bail, Context, Result};

use crate::common::read_file_to_u64;

pub const RESOURCED_CONFIG_PATH: &str = "run/chromeos-config/v1/resource/";

pub trait ConfigProvider {
    fn read_power_preferences(
        &self,
        power_source_type: PowerSourceType,
        power_preference_type: PowerPreferencesType,
    ) -> Result<Option<PowerPreferences>>;
}

/* TODO: Can we use `rust-protobuf` to generate all the structs? */

#[derive(Debug, PartialEq)]
pub enum Governor {
    OndemandGovernor { powersave_bias: u32 },
}

#[derive(Debug, PartialEq)]
pub struct PowerPreferences {
    pub governor: Option<Governor>,
    /* TODO: Add Intel EPP settings */
}

#[derive(Copy, Clone)]
pub enum PowerPreferencesType {
    Default,
    WebRTC,
    Fullscreen,
    Gaming,
}

impl PowerPreferencesType {
    fn to_name(self) -> &'static str {
        match self {
            PowerPreferencesType::Default => "default-power-preferences",
            PowerPreferencesType::WebRTC => "web-rtc-power-preferences",
            PowerPreferencesType::Fullscreen => "fullscreen-power-preferences",
            PowerPreferencesType::Gaming => "gaming-power-preferences",
        }
    }
}

#[derive(Copy, Clone)]
pub enum PowerSourceType {
    AC,
    DC,
}

impl PowerSourceType {
    fn to_name(self) -> &'static str {
        match self {
            PowerSourceType::AC => "ac",
            PowerSourceType::DC => "dc",
        }
    }
}

fn parse_ondemand_governor(path: &Path) -> Result<Governor> {
    let path = path.join("powersave-bias");

    let powersave_bias = read_file_to_u64(&path)
        .with_context(|| format!("Error reading powersave_bias from {}", path.display()))?
        as u32;

    Ok(Governor::OndemandGovernor { powersave_bias })
}

// Returns Ok(None) when there is no sub directory in path.
// Returns error when there are multiple sub directories in path or when the
// sub directory name is not a supported governor.
fn parse_governor(path: &Path) -> Result<Option<Governor>> {
    let mut dirs = path
        .read_dir()
        .with_context(|| format!("Failed to read governors from {}", path.display()))?;

    let first_dir = match dirs.next() {
        None => return Ok(None),
        Some(dir) => dir?,
    };

    if dirs.next().is_some() {
        bail!("Multiple governors detected in {}", path.display());
    }

    if first_dir.file_name() == "ondemand" {
        Ok(Some(parse_ondemand_governor(&first_dir.path())?))
    } else {
        bail!("Unknown governor {:?}!", first_dir.file_name())
    }
}

/* Expects to find a directory tree as follows:
 * * {root}/run/chromeos-config/v1/resource/
 *   * {ac,dc}
 *     * web-rtc-power-preferences/governor/
 *       * ondemand/
 *         * powersave-bias
 *     * fullscreen-power-preferences/governor/..
 *     * gaming-power-preferences/governor/..
 *     * default-power-preferences/governor/..
 */
#[derive(Debug)]
pub struct DirectoryConfigProvider<'a> {
    pub root: &'a str,
}

impl ConfigProvider for DirectoryConfigProvider<'_> {
    fn read_power_preferences(
        &self,
        power_source_type: PowerSourceType,
        power_preference_type: PowerPreferencesType,
    ) -> Result<Option<PowerPreferences>> {
        let path = Path::new(&self.root)
            .join(RESOURCED_CONFIG_PATH)
            .join(power_source_type.to_name())
            .join(power_preference_type.to_name());

        if !path.exists() {
            return Ok(None);
        }

        let mut preferences: PowerPreferences = PowerPreferences { governor: None };

        let governor_path = path.join("governor");
        if governor_path.exists() {
            preferences.governor = parse_governor(&governor_path)?;
        }

        Ok(Some(preferences))
    }
}
