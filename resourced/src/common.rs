// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::Mutex;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use log::error;
use log::warn;
use once_cell::sync::Lazy;

use crate::power;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::borealis::apply_borealis_tuning;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::cgroup_x86_64::media_dynamic_cgroup;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::cgroup_x86_64::MediaDynamicCgroupAction;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::globals::read_dynamic_epp_feature;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::globals::set_media_cgroup_state;
#[cfg(target_arch = "x86_64")]
use crate::x86_64::gpu_freq_scaling::intel_device;

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

static GAME_MODE: Lazy<Mutex<GameMode>> = Lazy::new(|| Mutex::new(GameMode::Off));

pub struct TuneSwappiness {
    pub swappiness: u32,
}

// Returns a TuneSwappiness object when swappiness needs to be tuned after setting game mode.
pub fn set_game_mode(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: GameMode,
    root: PathBuf,
) -> Result<Option<TuneSwappiness>> {
    let old_mode = match GAME_MODE.lock() {
        Ok(mut data) => {
            let old_data = *data;
            *data = mode;
            old_data
        }
        Err(_) => bail!("Failed to set game mode"),
    };

    // Don't fail game mode settings if EPP can't be changed.
    if let Err(e) = update_power_preferences(power_preference_manager) {
        warn!(
            "Unable to set EPP {:?}.  Continue setting other game mode options.",
            e
        );
    }

    #[cfg(target_arch = "x86_64")]
    if old_mode != GameMode::Borealis && mode == GameMode::Borealis {
        Ok(apply_borealis_tuning(&root, true))
    } else if old_mode == GameMode::Borealis && mode != GameMode::Borealis {
        Ok(apply_borealis_tuning(&root, false))
    } else {
        Ok(None)
    }

    #[cfg(not(target_arch = "x86_64"))]
    Ok(None)
}

pub fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
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

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
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

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum VmBootMode {
    // VM boot mode is not active.
    Inactive = 0,
    // VM boot mode active.
    Active = 1,
}
static VMBOOT_MODE: Lazy<Mutex<VmBootMode>> = Lazy::new(|| Mutex::new(VmBootMode::Inactive));

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
static BATTERY_SAVER_MODE: Lazy<Mutex<BatterySaverMode>> =
    Lazy::new(|| Mutex::new(BatterySaverMode::Inactive));

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

pub fn update_power_preferences(
    power_preference_manager: &dyn power::PowerPreferencesManager,
) -> Result<()> {
    // We need to ensure that any function that locks more than one lock does it
    // in the same order to avoid any dead locks.
    match RTC_AUDIO_ACTIVE.lock() {
        Ok(rtc_data) => match FULLSCREEN_VIDEO.lock() {
            Ok(fsv_data) => match GAME_MODE.lock() {
                Ok(game_data) => match VMBOOT_MODE.lock() {
                    Ok(boot_data) => match BATTERY_SAVER_MODE.lock() {
                        Ok(bsm_data) => power_preference_manager.update_power_preferences(
                            *rtc_data, *fsv_data, *game_data, *boot_data, *bsm_data,
                        )?,
                        Err(_) => bail!("Failed to get battery saver mode"),
                    },
                    Err(_) => bail!("Failed to get VM boot mode"),
                },
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
            #[cfg(target_arch = "x86_64")]
            if let Err(err) = set_gt_boost_freq_mhz(mode) {
                error!("Set boost freq not supported: {:#}", err)
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
    let dynamic_epp = read_dynamic_epp_feature();
    // Send signal to Auto EPP when MediaDynamicCgroup is enabled
    #[cfg(target_arch = "x86_64")]
    if dynamic_epp {
        set_media_cgroup_state(mode == FullscreenVideo::Active);
    }

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

pub fn on_battery_saver_mode_change(
    power_preference_manager: &dyn power::PowerPreferencesManager,
    mode: BatterySaverMode,
) -> Result<()> {
    match BATTERY_SAVER_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
        }
        Err(_) => bail!("Failed to set Battery saver mode activity"),
    }

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
    match VMBOOT_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
        }
        Err(_) => bail!("Failed to set VM boot mode activity"),
    }

    update_power_preferences(power_preference_manager)?;

    Ok(())
}

#[cfg(target_arch = "x86_64")]
fn set_gt_boost_freq_mhz(mode: RTCAudioActive) -> Result<()> {
    set_gt_boost_freq_mhz_impl(Path::new("/"), mode)
}

// Extract the impl function for unittest.
#[cfg(target_arch = "x86_64")]
fn set_gt_boost_freq_mhz_impl(root: &Path, mode: RTCAudioActive) -> Result<()> {
    let mut gpu_config = intel_device::IntelGpuDeviceConfig::new(root.to_owned(), 100)?;
    gpu_config.set_rtc_audio_active(mode == RTCAudioActive::Active)
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
    #[cfg(target_arch = "x86_64")]
    fn test_set_gt_boost_freq_mhz() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();

        setup_mock_intel_gpu_dev_dirs(root);
        setup_mock_intel_gpu_files(root);
        write_mock_cpuinfo(
            root,
            "filter_out",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );
        set_gt_boost_freq_mhz_impl(root, RTCAudioActive::Active)
            .expect_err("Should return error on non-intel CPUs");

        write_mock_cpuinfo(
            root,
            "GenuineIntel",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );
        set_intel_gpu_min(root, 300);
        set_intel_gpu_max(root, 1100);

        set_intel_gpu_boost(root, 0);
        set_gt_boost_freq_mhz_impl(root, RTCAudioActive::Active)
            .expect_err("Should return error when gpu_boost is 0");

        set_intel_gpu_boost(root, 500);
        set_gt_boost_freq_mhz_impl(root, RTCAudioActive::Active).unwrap();

        assert_eq!(get_intel_gpu_boost(root), 300);

        set_gt_boost_freq_mhz_impl(root, RTCAudioActive::Inactive).unwrap();

        assert_eq!(get_intel_gpu_boost(root), 1100);
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
        assert_eq!(get_game_mode().unwrap(), GameMode::Off);
        set_game_mode(&power_manager, GameMode::Borealis, root.to_path_buf()).unwrap();
        assert_eq!(get_game_mode().unwrap(), GameMode::Borealis);
        set_game_mode(&power_manager, GameMode::Arc, root.to_path_buf()).unwrap();
        assert_eq!(get_game_mode().unwrap(), GameMode::Arc);
    }

    #[test]
    fn test_get_set_rtc_audio_active() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();
        test_write_cpuset_root_cpus(root, "0-3");
        let power_manager = MockPowerPreferencesManager {
            root: root.to_path_buf(),
        };

        assert_eq!(get_rtc_audio_active().unwrap(), RTCAudioActive::Inactive);
        set_rtc_audio_active(&power_manager, RTCAudioActive::Active).unwrap();
        assert_eq!(get_rtc_audio_active().unwrap(), RTCAudioActive::Active);
    }

    #[test]
    fn test_get_set_fullscreen_video() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();
        test_write_cpuset_root_cpus(root, "0-3");
        let power_manager = MockPowerPreferencesManager {
            root: root.to_path_buf(),
        };

        assert_eq!(get_fullscreen_video().unwrap(), FullscreenVideo::Inactive);
        set_fullscreen_video(&power_manager, FullscreenVideo::Active).unwrap();
        assert_eq!(get_fullscreen_video().unwrap(), FullscreenVideo::Active);
    }
}
