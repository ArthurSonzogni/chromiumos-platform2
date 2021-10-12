// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use once_cell::sync::Lazy;

// Extract the parsing function for unittest.
pub fn parse_file_to_u64<R: BufRead>(reader: R) -> Result<u64> {
    let first_line = reader.lines().next().context("No content in buffer")??;
    first_line
        .parse()
        .with_context(|| format!("Couldn't parse \"{}\" as u64", first_line))
}

/// Get the first line in a file and parse as u64.
pub fn read_file_to_u64<P: AsRef<Path>>(filename: P) -> Result<u64> {
    let reader = File::open(filename).map(BufReader::new)?;
    parse_file_to_u64(reader)
}

#[derive(Clone, Copy, PartialEq)]
pub enum GameMode {
    // Game mode is off.
    Off = 0,
    // Game mode is on, borealis is the foreground subsystem.
    Borealis = 1,
}

impl TryFrom<u8> for GameMode {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<GameMode> {
        Ok(match mode_raw {
            0 => GameMode::Off,
            1 => GameMode::Borealis,
            _ => bail!("Unsupported game mode value"),
        })
    }
}

static GAME_MODE: Lazy<Mutex<GameMode>> = Lazy::new(|| Mutex::new(GameMode::Off));

pub fn set_game_mode(mode: GameMode) -> Result<()> {
    match GAME_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
            Ok(())
        }
        Err(_) => bail!("Failed to set game mode"),
    }
}

pub fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum RTCAudioActive {
    // RTC is not active.
    Inactive = 0,
    // RTC is active, RTC audio is playing and recording.
    Active = 1,
}

impl TryFrom<u8> for RTCAudioActive {
    type Error = anyhow::Error;

    fn try_from(active_raw: u8) -> Result<RTCAudioActive> {
        Ok(match active_raw {
            0 => RTCAudioActive::Inactive,
            1 => RTCAudioActive::Active,
            _ => bail!("Unsupported RTC audio active value"),
        })
    }
}

static RTC_ACTIVE: Lazy<Mutex<RTCAudioActive>> = Lazy::new(|| Mutex::new(RTCAudioActive::Inactive));

// Set EPP value in sysfs for Intel devices with X86_FEATURE_HWP_EPP support.
// On !X86_FEATURE_HWP_EPP Intel devices, an integer write to the sysfs node
// will fail with -EINVAL.
pub fn set_epp(root_path: &str, value: &str) -> Result<()> {
    let entries = std::fs::read_dir(root_path.to_string() + "/sys/devices/system/cpu/cpufreq/")?;

    for entry in entries {
        let entry_str = entry.map(|e| e.path())?.display().to_string();

        if entry_str.contains("policy") {
            let file_path = entry_str + "/energy_performance_preference";
            match std::fs::write(&file_path, value) {
                Ok(_) => (),
                Err(_) => bail!("Failed to set EPP sysfs value!"),
            }
        }
    }

    Ok(())
}

pub fn set_rtc_audio_active(mode: RTCAudioActive) -> Result<()> {
    match RTC_ACTIVE.lock() {
        Ok(mut data) => {
            *data = mode;

            let epp_value = if mode == RTCAudioActive::Active {
                "179" // Set EPP to 70%
            } else {
                "balance_performance" // Default EPP
            };

            set_epp("/", epp_value).context("Failed to set EPP sysfs value!")
        }

        Err(_) => bail!("Failed to set RTC audio activity"),
    }
}

pub fn get_rtc_audio_active() -> Result<RTCAudioActive> {
    match RTC_ACTIVE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get status of RTC audio activity"),
    }
}
