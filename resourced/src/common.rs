// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fs::read_to_string;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::Arc;
use std::sync::Mutex;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use glob::glob;
use log::warn;

use crate::arch;
use crate::config::ThermalConfig;
use crate::power;
use crate::sync::NoPoison;

const THERMAL_ZONE_TYPE_PATTERN: &str = "sys/class/thermal/thermal_zone*/type";
const THERMAL_ZONE_TEMP: &str = "temp";

/// Parse the first line of a file as a type implementing std::str::FromStr.
pub fn read_from_file<T: FromStr, P: AsRef<Path>>(path: &P) -> Result<T>
where
    T::Err: std::error::Error + Sync + Send + 'static,
{
    let reader = File::open(path).map(BufReader::new)?;
    let line = reader.lines().next().context("No content in file")??;
    line.parse()
        .with_context(|| format!("failed to parse \"{line}\""))
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
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

static GAME_MODE: Mutex<GameMode> = Mutex::new(GameMode::Off);

#[derive(Clone, Copy)]
pub struct TuneSwappiness {
    pub swappiness: u32,
}

// Returns a TuneSwappiness object when swappiness needs to be tuned after setting game mode.
pub fn set_game_mode(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: GameMode,
    root: PathBuf,
) -> Option<TuneSwappiness> {
    let old_mode = {
        let mut data = GAME_MODE.do_lock();
        let old_mode = *data;
        *data = mode;
        old_mode
    };

    // Don't fail game mode settings if EPP can't be changed.
    if let Err(e) = update_power_preferences(power_preference_manager) {
        warn!(
            "Unable to set EPP {:?}.  Continue setting other game mode options.",
            e
        );
    }

    if old_mode != GameMode::Borealis && mode == GameMode::Borealis {
        arch::apply_borealis_tuning(&root, true)
    } else if old_mode == GameMode::Borealis && mode != GameMode::Borealis {
        arch::apply_borealis_tuning(&root, false)
    } else {
        None
    }
}

pub fn get_game_mode() -> GameMode {
    *GAME_MODE.do_lock()
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RTCAudioActive {
    // RTC is not active.
    Inactive = 0,
    // RTC is active, RTC audio is playing and recording.
    Active = 1,
}
static RTC_AUDIO_ACTIVE: Mutex<RTCAudioActive> = Mutex::new(RTCAudioActive::Inactive);

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

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FullscreenVideo {
    // Full screen video is not active.
    Inactive = 0,
    // Full screen video is active.
    Active = 1,
}
static FULLSCREEN_VIDEO: Mutex<FullscreenVideo> = Mutex::new(FullscreenVideo::Inactive);

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

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum VmBootMode {
    // VM boot mode is not active.
    Inactive = 0,
    // VM boot mode active.
    Active = 1,
}
static VMBOOT_MODE: Mutex<VmBootMode> = Mutex::new(VmBootMode::Inactive);

impl TryFrom<u8> for VmBootMode {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<VmBootMode> {
        Ok(match mode_raw {
            0 => VmBootMode::Inactive,
            1 => VmBootMode::Active,
            _ => bail!("Unsupported VM boot mode"),
        })
    }
}

pub fn is_vm_boot_mode_enabled() -> bool {
    // Since this vm_boot_mode has no performance impact on lower end x86_64
    // devices and may hit thermal throttole on high end x86_64 ddevices,
    // this feature is disabled on x86_64.
    #[cfg(not(target_arch = "x86_64"))]
    return true;
    #[cfg(target_arch = "x86_64")]
    return false;
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BatterySaverMode {
    // Battery saver mode is not active.
    Inactive = 0,
    // Battery saver mode is active.
    Active = 1,
}
static BATTERY_SAVER_MODE: Mutex<BatterySaverMode> = Mutex::new(BatterySaverMode::Inactive);

impl TryFrom<u8> for BatterySaverMode {
    type Error = anyhow::Error;

    fn try_from(active_raw: u8) -> Result<BatterySaverMode> {
        Ok(match active_raw {
            0 => BatterySaverMode::Inactive,
            1 => BatterySaverMode::Active,
            _ => bail!("Unsupported battery saver mode value"),
        })
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ThermalState {
    Normal = 0,
    Stress = 1,
}
static THERMAL_STATE: Mutex<ThermalState> = Mutex::new(ThermalState::Normal);

impl TryFrom<u8> for ThermalState {
    type Error = anyhow::Error;

    fn try_from(active_raw: u8) -> Result<ThermalState> {
        Ok(match active_raw {
            0 => ThermalState::Normal,
            1 => ThermalState::Stress,
            _ => bail!("Unsupported thermal state value"),
        })
    }
}

fn find_thermal_zone(
    root: PathBuf,
    thermal_config_mutex: &Mutex<ThermalConfig>,
) -> Result<Option<PathBuf>> {
    let thermal_config = thermal_config_mutex.do_lock();

    let pattern = root
        .join(THERMAL_ZONE_TYPE_PATTERN)
        .to_str()
        .context(format!(
            "Failed to construct {THERMAL_ZONE_TYPE_PATTERN} glob pattern"
        ))?
        .to_owned();

    for thermal_zone_type in
        (glob(&pattern).context(format!("Failed to read {pattern} glob pattern"))?).flatten()
    {
        if let Ok(content) = read_to_string(&thermal_zone_type) {
            if content.trim() == thermal_config.thermal_type {
                let mut temp_path = thermal_zone_type.clone();
                temp_path.set_file_name(THERMAL_ZONE_TEMP);
                if temp_path.exists() {
                    return Ok(Some(temp_path));
                }
            }
        }
    }

    Ok(None)
}

pub fn thermal_state_callback(
    root: PathBuf,
    thermal_config_mutex: Arc<Mutex<ThermalConfig>>,
) -> Option<impl Fn(ThermalState) -> Result<ThermalState>> {
    let thermal_path =
        find_thermal_zone(root, thermal_config_mutex.as_ref()).unwrap_or_else(|e| {
            warn!("Error finding thermal zone, error: {}", e);
            None
        })?;

    Some(move |thermal_state| -> Result<ThermalState> {
        let thermal_config = thermal_config_mutex.do_lock();

        let temp: i32 = read_from_file(&thermal_path).context(format!(
            "Error reading temp from {}",
            thermal_path.display()
        ))?;

        if (thermal_state == ThermalState::Normal && temp >= thermal_config.trip_temp)
            || (thermal_state == ThermalState::Stress
                && temp >= thermal_config.trip_temp - thermal_config.hysteresis as i32)
        {
            Ok(ThermalState::Stress)
        } else {
            Ok(ThermalState::Normal)
        }
    })
}

pub fn update_thermal_state(
    read_thermal_state: &impl Fn(ThermalState) -> Result<ThermalState>,
) -> bool {
    let mut thermal_state = THERMAL_STATE.do_lock();

    let prev_thermal_state = *thermal_state;

    match read_thermal_state(prev_thermal_state) {
        Ok(new_thermal_state) => {
            *thermal_state = new_thermal_state;

            new_thermal_state != prev_thermal_state
        }
        Err(_) => false,
    }
}

pub fn get_thermal_state() -> ThermalState {
    *THERMAL_STATE.do_lock()
}

pub fn update_power_preferences(
    power_preference_manager: &dyn power::PowerPreferencesManager,
) -> Result<()> {
    // We need to ensure that any function that locks more than one lock does it
    // in the same order to avoid any dead locks.
    let rtc_data = RTC_AUDIO_ACTIVE.do_lock();
    let fsv_data = FULLSCREEN_VIDEO.do_lock();
    let game_data = GAME_MODE.do_lock();
    let boot_data = VMBOOT_MODE.do_lock();
    let bsm_data = BATTERY_SAVER_MODE.do_lock();
    let thermal_state = THERMAL_STATE.do_lock();
    power_preference_manager.update_power_preferences(
        *rtc_data,
        *fsv_data,
        *game_data,
        *boot_data,
        *bsm_data,
        *thermal_state,
    )
}

pub fn set_rtc_audio_active(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: RTCAudioActive,
) -> Result<()> {
    *RTC_AUDIO_ACTIVE.do_lock() = mode;

    update_power_preferences(power_preference_manager)?;
    Ok(())
}

pub fn get_rtc_audio_active() -> RTCAudioActive {
    *RTC_AUDIO_ACTIVE.do_lock()
}

pub fn set_fullscreen_video(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: FullscreenVideo,
) -> Result<()> {
    *FULLSCREEN_VIDEO.do_lock() = mode;

    update_power_preferences(power_preference_manager)?;

    Ok(())
}

pub fn get_fullscreen_video() -> FullscreenVideo {
    *FULLSCREEN_VIDEO.do_lock()
}

pub fn on_battery_saver_mode_change(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: BatterySaverMode,
) -> Result<()> {
    *BATTERY_SAVER_MODE.do_lock() = mode;

    update_power_preferences(power_preference_manager)?;

    Ok(())
}

pub fn set_vm_boot_mode(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: VmBootMode,
) -> Result<()> {
    if !is_vm_boot_mode_enabled() {
        bail!("VM boot mode is not enabled");
    }
    *VMBOOT_MODE.do_lock() = mode;

    update_power_preferences(power_preference_manager)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use std::io::Write;

    use tempfile::tempdir;
    use tempfile::NamedTempFile;

    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_read_from_file() {
        for (content, expected) in [("123", 123), ("456\n789", 456)] {
            let mut file = NamedTempFile::new().unwrap();
            file.write_all(content.as_bytes()).unwrap();

            assert_eq!(
                read_from_file::<u64, _>(&file.path()).unwrap(),
                expected as u64
            );
            assert_eq!(
                read_from_file::<u32, _>(&file.path()).unwrap(),
                expected as u32
            );
            assert_eq!(
                read_from_file::<i64, _>(&file.path()).unwrap(),
                expected as i64
            );
            assert_eq!(read_from_file::<i32, _>(&file.path()).unwrap(), expected);
        }

        for (negative_content, expected) in [("-123", -123), ("-456\n789", -456)] {
            let mut file = NamedTempFile::new().unwrap();
            file.write_all(negative_content.as_bytes()).unwrap();

            assert_eq!(
                read_from_file::<i64, _>(&file.path()).unwrap(),
                expected as i64
            );
            assert_eq!(read_from_file::<i32, _>(&file.path()).unwrap(), expected);

            assert!(read_from_file::<u64, _>(&file.path()).is_err());
            assert!(read_from_file::<u32, _>(&file.path()).is_err());
        }

        for wrong_content in ["", "abc"] {
            let mut file = NamedTempFile::new().unwrap();
            file.write_all(wrong_content.as_bytes()).unwrap();

            assert!(read_from_file::<u64, _>(&file.path()).is_err());
        }
    }

    #[test]
    fn test_get_set_game_mode() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();
        setup_mock_cpu_dev_dirs(root).unwrap();
        setup_mock_cpu_files(root).unwrap();
        setup_mock_intel_gpu_dev_dirs(root);
        setup_mock_intel_gpu_files(root);
        let power_manager = MockPowerPreferencesManager {
            root: root.to_path_buf(),
        };
        assert_eq!(get_game_mode(), GameMode::Off);
        set_game_mode(&power_manager, GameMode::Borealis, root.to_path_buf());
        assert_eq!(get_game_mode(), GameMode::Borealis);
        set_game_mode(&power_manager, GameMode::Arc, root.to_path_buf());
        assert_eq!(get_game_mode(), GameMode::Arc);
    }

    #[test]
    fn test_get_set_rtc_audio_active() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();
        test_write_cpuset_root_cpus(root, "0-3");
        let power_manager = MockPowerPreferencesManager {
            root: root.to_path_buf(),
        };

        assert_eq!(get_rtc_audio_active(), RTCAudioActive::Inactive);
        set_rtc_audio_active(&power_manager, RTCAudioActive::Active).unwrap();
        assert_eq!(get_rtc_audio_active(), RTCAudioActive::Active);
    }

    #[test]
    fn test_get_set_fullscreen_video() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();
        test_write_cpuset_root_cpus(root, "0-3");
        let power_manager = MockPowerPreferencesManager {
            root: root.to_path_buf(),
        };

        assert_eq!(get_fullscreen_video(), FullscreenVideo::Inactive);
        set_fullscreen_video(&power_manager, FullscreenVideo::Active).unwrap();
        assert_eq!(get_fullscreen_video(), FullscreenVideo::Active);
    }
}
