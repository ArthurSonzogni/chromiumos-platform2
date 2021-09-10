// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use glob::glob;
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
static RTC_AUDIO_ACTIVE: Lazy<Mutex<RTCAudioActive>> =
    Lazy::new(|| Mutex::new(RTCAudioActive::Inactive));

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

#[derive(Clone, Copy, PartialEq)]
pub enum FullscreenVideo {
    // Full screen video is not active.
    Inactive = 0,
    // Full screen video is active.
    Active = 1,
}
static FULLSCREEN_VIDEO: Lazy<Mutex<FullscreenVideo>> =
    Lazy::new(|| Mutex::new(FullscreenVideo::Inactive));

impl TryFrom<u8> for FullscreenVideo {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<FullscreenVideo> {
        Ok(match mode_raw {
            0 => FullscreenVideo::Inactive,
            1 => FullscreenVideo::Active,
            _ => bail!("Unsupported fullscreen video mode"),
        })
    }
}

// Set EPP value in sysfs for Intel devices with X86_FEATURE_HWP_EPP support.
// On !X86_FEATURE_HWP_EPP Intel devices, an integer write to the sysfs node
// will fail with -EINVAL.
pub fn set_epp(root_path: &str, value: &str) -> Result<()> {
    let pattern = root_path.to_owned()
        + "/sys/devices/system/cpu/cpufreq/policy*/energy_performance_preference";

    for entry in glob(&pattern)? {
        std::fs::write(&entry?, value).context("Failed to set EPP sysfs value!")?;
    }

    Ok(())
}

pub fn update_epp_if_needed() -> Result<()> {
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(rtc_data) => {
            match FULLSCREEN_VIDEO.lock() {
                Ok(fsv_data) => {
                    if *rtc_data == RTCAudioActive::Active || *fsv_data == FullscreenVideo::Active {
                        set_epp("/", "179")?; // Set EPP to 70%
                    } else if *rtc_data != RTCAudioActive::Active
                        && *fsv_data != FullscreenVideo::Active
                    {
                        set_epp("/", "balance_performance")?; // Default EPP
                    }
                }
                Err(_) => bail!("Failed to update EPP!"),
            }
        }
        Err(_) => bail!("Failed to update EPP!"),
    }
    Ok(())
}

pub fn set_rtc_audio_active(mode: RTCAudioActive) -> Result<()> {
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(mut data) => {
            *data = mode;

            let epp_value = if mode == RTCAudioActive::Active {
                "179" // Set EPP to 70%
            } else {
                "balance_performance" // Default EPP
            };

            set_epp("/", epp_value).context("Failed to set EPP sysfs value!")?;
        }
        Err(_) => bail!("Failed to set RTC audio activity"),
    }
    update_epp_if_needed()?;
    Ok(())
}

pub fn get_rtc_audio_active() -> Result<RTCAudioActive> {
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get status of RTC audio activity"),
    }
}

pub fn set_fullscreen_video(mode: FullscreenVideo) -> Result<()> {
    match FULLSCREEN_VIDEO.lock() {
        Ok(mut data) => {
            *data = mode;
        }
        Err(_) => bail!("Failed to set full screen video activity"),
    }
    update_epp_if_needed()?;
    Ok(())
}

pub fn get_fullscreen_video() -> Result<FullscreenVideo> {
    match FULLSCREEN_VIDEO.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get full screen video activity"),
    }
}
