// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use libchromeos::sys::error;
use once_cell::sync::Lazy;

use crate::power;

#[cfg(target_arch = "x86_64")]
use crate::cgroup_x86_64::{media_dynamic_cgroup, MediaDynamicCgroupAction};

// Extract the parsing function for unittest.
fn parse_file_to_u64<R: BufRead>(reader: R) -> Result<u64> {
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

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum GameMode {
    // Game mode is off.
    Off = 0,
    // Game mode is on, borealis is the foreground subsystem.
    Borealis = 1,
    // Game mode for ARC is on, which means we shouldn't evict Android apps that
    // are foreground or perceptible.
    Arc = 2,
}

impl TryFrom<u8> for GameMode {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<GameMode> {
        Ok(match mode_raw {
            0 => GameMode::Off,
            1 => GameMode::Borealis,
            2 => GameMode::Arc,
            _ => bail!("Unsupported game mode value"),
        })
    }
}

static GAME_MODE: Lazy<Mutex<GameMode>> = Lazy::new(|| Mutex::new(GameMode::Off));

pub struct TuneSwappiness {
    pub swappiness: u32,
}

// Returns a TuneSwappiness object when swappiness needs to be tuned after setting game mode.
pub fn set_game_mode(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: GameMode,
) -> Result<Option<TuneSwappiness>> {
    let old_mode = match GAME_MODE.lock() {
        Ok(mut data) => {
            let old_data = *data;
            *data = mode;
            old_data
        }
        Err(_) => bail!("Failed to set game mode"),
    };

    update_power_preferences(power_preference_manager)?;

    const TUNED_SWAPPINESS_VALUE: u32 = 30;
    const DEFAULT_SWAPPINESS_VALUE: u32 = 60;

    if old_mode != GameMode::Borealis && mode == GameMode::Borealis {
        Ok(Some(TuneSwappiness {
            swappiness: TUNED_SWAPPINESS_VALUE,
        }))
    } else if old_mode == GameMode::Borealis && mode != GameMode::Borealis {
        Ok(Some(TuneSwappiness {
            swappiness: DEFAULT_SWAPPINESS_VALUE,
        }))
    } else {
        Ok(None)
    }
}

pub fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
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

#[derive(Clone, Copy, PartialEq, Debug)]
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

    #[cfg(target_arch = "x86_64")]
    match mode {
        FullscreenVideo::Active => media_dynamic_cgroup(MediaDynamicCgroupAction::Start)?,
        FullscreenVideo::Inactive => media_dynamic_cgroup(MediaDynamicCgroupAction::Stop)?,
    }

    Ok(())
}

pub fn get_fullscreen_video() -> Result<FullscreenVideo> {
    match FULLSCREEN_VIDEO.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get full screen video activity"),
    }
}

fn set_gt_boost_freq_mhz(mode: RTCAudioActive) -> Result<()> {
    set_gt_boost_freq_mhz_impl(Path::new("/"), mode)
}

// Extract the impl function for unittest.
fn set_gt_boost_freq_mhz_impl(root: &Path, mode: RTCAudioActive) -> Result<()> {
    const SYSFS_BASE: &str = "sys/class/drm";
    const BOOST_PATH: &str = "gt_boost_freq_mhz";
    const MIN_PATH: &str = "gt_min_freq_mhz";
    const MAX_PATH: &str = "gt_max_freq_mhz";

    let drm_card_glob = root.join(SYSFS_BASE).join("card*").join(BOOST_PATH);
    let drm_card_glob_str = drm_card_glob
        .to_str()
        .with_context(|| format!("cannot convert path to str: {}", drm_card_glob.display()))?;
    let first_glob_result = match glob::glob(drm_card_glob_str)?.next() {
        Some(result) => result?,
        None => return Ok(()), // Skip when there is no gt_boost_freq_mhz file.
    };
    let card_path = first_glob_result
        .parent()
        .with_context(|| format!("cannot get parent: {}", first_glob_result.display()))?
        .to_path_buf();

    let gt_boost_freq_mhz_content = std::fs::read_to_string(card_path.join(BOOST_PATH))?;
    let curent_gt_boost_freq_mhz = gt_boost_freq_mhz_content.parse::<i32>().with_context(|| {
        format!(
            "failed to parse gt_boost_freq_mhz to i32: {}",
            gt_boost_freq_mhz_content
        )
    })?;

    // When gt_boost_freq_mhz is 0, it cannot be changed.
    if curent_gt_boost_freq_mhz == 0 {
        return Ok(());
    }

    let gt_boost_freq_mhz = if mode == RTCAudioActive::Active {
        std::fs::read_to_string(card_path.join(MIN_PATH))?
    } else {
        std::fs::read_to_string(card_path.join(MAX_PATH))?
    };

    std::fs::write(card_path.join(BOOST_PATH), gt_boost_freq_mhz)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn test_parse_file_to_u64() {
        assert_eq!(
            parse_file_to_u64("123".to_string().as_bytes()).unwrap(),
            123
        );
        assert_eq!(
            parse_file_to_u64("456\n789".to_string().as_bytes()).unwrap(),
            456
        );
        assert!(parse_file_to_u64("".to_string().as_bytes()).is_err());
        assert!(parse_file_to_u64("abc".to_string().as_bytes()).is_err());
    }

    fn write_i32_to_file(path: &Path, value: i32) {
        fs::create_dir_all(
            path.parent()
                .with_context(|| format!("cannot get parent: {}", path.display()))
                .unwrap(),
        )
        .unwrap();

        std::fs::write(path, value.to_string()).unwrap();
    }

    #[test]
    fn test_set_gt_boost_freq_mhz() {
        const GT_BOOST_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_boost_freq_mhz";
        const GT_MAX_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_max_freq_mhz";
        const GT_MIN_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_min_freq_mhz";

        let root = tempdir().unwrap();
        let root_path = root.path();
        let gt_boost_freq_mhz_path = root_path.join(GT_BOOST_FREQ_MHZ_PATH);
        let gt_max_freq_mhz_path = root_path.join(GT_MAX_FREQ_MHZ_PATH);
        let gt_min_freq_mhz_path = root_path.join(GT_MIN_FREQ_MHZ_PATH);
        write_i32_to_file(&gt_boost_freq_mhz_path, 500);
        write_i32_to_file(&gt_max_freq_mhz_path, 1100);
        write_i32_to_file(&gt_min_freq_mhz_path, 300);

        set_gt_boost_freq_mhz_impl(root_path, RTCAudioActive::Active).unwrap();

        assert_eq!(read_file_to_u64(&gt_boost_freq_mhz_path).unwrap(), 300);

        set_gt_boost_freq_mhz_impl(root_path, RTCAudioActive::Inactive).unwrap();

        assert_eq!(read_file_to_u64(&gt_boost_freq_mhz_path).unwrap(), 1100);
    }

    struct MockPowerPreferencesManager {}
    impl power::PowerPreferencesManager for MockPowerPreferencesManager {
        fn update_power_preferences(
            &self,
            _rtc: RTCAudioActive,
            _fullscreen: FullscreenVideo,
            _game: GameMode,
        ) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn test_get_set_game_mode() {
        let power_manager = MockPowerPreferencesManager {};

        assert_eq!(get_game_mode().unwrap(), GameMode::Off);
        set_game_mode(&power_manager, GameMode::Borealis).unwrap();
        assert_eq!(get_game_mode().unwrap(), GameMode::Borealis);
        set_game_mode(&power_manager, GameMode::Arc).unwrap();
        assert_eq!(get_game_mode().unwrap(), GameMode::Arc);
    }

    #[test]
    fn test_get_set_rtc_audio_active() {
        let power_manager = MockPowerPreferencesManager {};

        assert_eq!(get_rtc_audio_active().unwrap(), RTCAudioActive::Inactive);
        set_rtc_audio_active(&power_manager, RTCAudioActive::Active).unwrap();
        assert_eq!(get_rtc_audio_active().unwrap(), RTCAudioActive::Active);
    }

    #[test]
    fn test_get_set_fullscreen_video() {
        let power_manager = MockPowerPreferencesManager {};

        assert_eq!(get_fullscreen_video().unwrap(), FullscreenVideo::Inactive);
        set_fullscreen_video(&power_manager, FullscreenVideo::Active).unwrap();
        assert_eq!(get_fullscreen_video().unwrap(), FullscreenVideo::Active);
    }
}
