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
use sys_util::error;

use crate::power;

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

pub fn set_game_mode(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: GameMode,
) -> Result<()> {
    match GAME_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
        }
        Err(_) => bail!("Failed to set game mode"),
    };

    update_power_preferences(power_preference_manager)?;

    Ok(())
}

pub fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

pub struct GtSysfsPaths {
    base: String,
    card: String,
    boost: String,
    min: String,
    max: String,
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

pub fn update_power_preferences(
    power_preference_manager: &dyn power::PowerPreferencesManager,
) -> Result<()> {
    // We need to ensure that any function that locks more than one lock does it
    // in the same order to avoid any dead locks.
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(rtc_data) => match FULLSCREEN_VIDEO.lock() {
            Ok(fsv_data) => match GAME_MODE.lock() {
                Ok(game_data) => power_preference_manager
                    .update_power_preferences(*rtc_data, *fsv_data, *game_data)?,
                Err(_) => bail!("Failed to get game mode"),
            },
            Err(_) => bail!("Failed to get fullscreen mode!"),
        },
        Err(_) => bail!("Failed to get rtd audio mode!"),
    }
    Ok(())
}

pub fn set_rtc_audio_active(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: RTCAudioActive,
) -> Result<()> {
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(mut data) => {
            *data = mode;
            /* TODO(b/226646450): set_gt_boost_freq_mhz fails on non-Intel platforms. */
            if let Err(err) = set_gt_boost_freq_mhz(mode) {
                error!("Failed to set boost freq: {:#}", err)
            }
        }
        Err(_) => bail!("Failed to set RTC audio activity"),
    }

    update_power_preferences(power_preference_manager)?;
    Ok(())
}

pub fn get_rtc_audio_active() -> Result<RTCAudioActive> {
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get status of RTC audio activity"),
    }
}

pub fn set_fullscreen_video(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: FullscreenVideo,
) -> Result<()> {
    match FULLSCREEN_VIDEO.lock() {
        Ok(mut data) => {
            *data = mode;
        }
        Err(_) => bail!("Failed to set full screen video activity"),
    }

    update_power_preferences(power_preference_manager)?;
    Ok(())
}

pub fn get_fullscreen_video() -> Result<FullscreenVideo> {
    match FULLSCREEN_VIDEO.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get full screen video activity"),
    }
}

pub fn read_gt_sysfs(gt_base_path: &str, gt_sysfs_file: &str) -> Result<String> {
    let gt_freq_src = format!("{}/{}", gt_base_path, gt_sysfs_file);
    std::fs::read_to_string(&gt_freq_src).context("Failed to read min_freq_src")
}

pub fn set_gt_boost_freq_mhz(mode: RTCAudioActive) -> Result<(), std::io::Error> {
    let mut gt_sysfs_paths = GtSysfsPaths {
        base: String::from("/sys/class/drm"),
        card: String::from("/"),
        boost: String::from("gt_boost_freq_mhz"),
        min: String::from("gt_min_freq_mhz"),
        max: String::from("gt_max_freq_mhz"),
    };

    let drm_card_glob = format!(
        "{}/{}/{}",
        &gt_sysfs_paths.base, "card*", &gt_sysfs_paths.boost
    );
    for entry in glob::glob(&drm_card_glob).unwrap() {
        match entry {
            Ok(path) => gt_sysfs_paths.card = format!("{}", path.parent().unwrap().display()),
            Err(e) => println!("{:?}", e),
        }
    }

    let gt_min_freq_mhz = read_gt_sysfs(&gt_sysfs_paths.card, &gt_sysfs_paths.min);
    let gt_max_freq_mhz = read_gt_sysfs(&gt_sysfs_paths.card, &gt_sysfs_paths.max);
    let gt_boost_freq_mhz = if mode == RTCAudioActive::Active {
        match &gt_min_freq_mhz {
            Ok(gt_boost_freq_mhz) => gt_boost_freq_mhz,
            Err(_) => "gt boost fail to read sysfs min freq",
        }
    } else {
        match &gt_max_freq_mhz {
            Ok(gt_boost_freq_mhz) => gt_boost_freq_mhz,
            Err(_) => "gt boost fail to read sysfs max freq",
        }
    };

    let full_gt_boost_path = format!("{}/{}", &gt_sysfs_paths.card, &gt_sysfs_paths.boost);
    std::fs::write(&full_gt_boost_path, &gt_boost_freq_mhz.as_bytes())
}
