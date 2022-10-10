// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::path::PathBuf;

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

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Governor {
    Conservative,
    Ondemand {
        powersave_bias: u32,
        sampling_rate: Option<u32>,
    },
    Performance,
    Powersave,
    Schedutil,
    Userspace,
}

impl Governor {
    pub fn to_name(self) -> &'static str {
        match self {
            Governor::Conservative => "conservative",
            Governor::Ondemand {
                powersave_bias: _,
                sampling_rate: _,
            } => "ondemand",
            Governor::Performance => "performance",
            Governor::Powersave => "powersave",
            Governor::Schedutil => "schedutil",
            Governor::Userspace => "userspace",
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct PowerPreferences {
    pub governor: Option<Governor>,
    /* TODO: Add Intel EPP settings */
}

#[derive(Copy, Clone)]
pub enum PowerPreferencesType {
    Default,
    WebRTC,
    Fullscreen,
    BorealisGaming,
    ArcvmGaming,
}

impl PowerPreferencesType {
    fn to_name(self) -> &'static str {
        match self {
            PowerPreferencesType::Default => "default-power-preferences",
            PowerPreferencesType::WebRTC => "web-rtc-power-preferences",
            PowerPreferencesType::Fullscreen => "fullscreen-power-preferences",
            PowerPreferencesType::BorealisGaming => "borealis-gaming-power-preferences",
            PowerPreferencesType::ArcvmGaming => "arcvm-gaming-power-preferences",
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
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
    let powersave_bias_path = path.join("powersave-bias");

    let powersave_bias = read_file_to_u64(&powersave_bias_path).with_context(|| {
        format!(
            "Error reading powersave-bias from {}",
            powersave_bias_path.display()
        )
    })? as u32;

    let sampling_rate_path = path.join("sampling-rate-ms");

    // The sampling-rate config is optional in the config
    let sampling_rate = if sampling_rate_path.exists() {
        let sampling_rate_ms = read_file_to_u64(&sampling_rate_path).with_context(|| {
            format!(
                "Error reading sampling-rate-ms from {}",
                sampling_rate_path.display()
            )
        })? as u32;

        // We treat the default value of 0 as unset. We do this because the kernel treats
        // a sampling rate of 0 as invalid.
        if sampling_rate_ms == 0 {
            None
        } else {
            // We convert from ms to uS to match what the kernel expects
            Some(sampling_rate_ms * 1000)
        }
    } else {
        None
    };

    Ok(Governor::Ondemand {
        powersave_bias,
        sampling_rate,
    })
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

    match first_dir.file_name().to_str() {
        Some("conservative") => Ok(Some(Governor::Conservative)),
        Some("ondemand") => Ok(Some(parse_ondemand_governor(&first_dir.path())?)),
        Some("performance") => Ok(Some(Governor::Performance)),
        Some("powersave") => Ok(Some(Governor::Powersave)),
        Some("schedutil") => Ok(Some(Governor::Schedutil)),
        Some("userspace") => Ok(Some(Governor::Userspace)),
        _ => bail!("Unknown governor {:?}!", first_dir.file_name()),
    }
}

/* Expects to find a directory tree as follows:
 * * {root}/run/chromeos-config/v1/resource/
 *   * {ac,dc}
 *     * web-rtc-power-preferences/governor/
 *       * ondemand/
 *         * powersave-bias
 *     * fullscreen-power-preferences/governor/
 *       * schedutil/
 *     * borealis-gaming-power-preferences/governor/..
 *     * arcvm-gaming-power-preferences/governor/..
 *     * default-power-preferences/governor/..
 */
#[derive(Debug)]
pub struct DirectoryConfigProvider {
    pub root: PathBuf,
}

impl ConfigProvider for DirectoryConfigProvider {
    fn read_power_preferences(
        &self,
        power_source_type: PowerSourceType,
        power_preference_type: PowerPreferencesType,
    ) -> Result<Option<PowerPreferences>> {
        let path = self
            .root
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
