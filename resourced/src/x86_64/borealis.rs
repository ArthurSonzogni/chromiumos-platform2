// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::bail;
use anyhow::Result;
use log::info;
use log::warn;
use once_cell::sync::Lazy;

use crate::common::read_from_file;
use crate::common::TuneSwappiness;
use crate::config::PowerSourceType;
use crate::memory;
use crate::power::DirectoryPowerSourceProvider;
use crate::power::PowerSourceProvider;
use crate::x86_64::cpu_scaling::double_min_freq;
use crate::x86_64::cpu_scaling::intel_i7_or_above;
use crate::x86_64::cpu_scaling::set_min_cpu_freq;
use crate::x86_64::gpu_freq_scaling::intel_device;

const GPU_TUNING_POLLING_INTERVAL_MS: u64 = 1000;

const TUNED_SWAPPINESS_VALUE: u32 = 30;
const DEFAULT_SWAPPINESS_VALUE: u32 = 60;

//TODO(syedfaaiz) : modify these values following a benchy run to 70/50.
const GAMEMODE_RPS_UP: u64 = 95;
const GAMEMODE_RPS_DOWN: u64 = 85;

// Paths for RPS up/down threshold relative to rootdir.
const DEVICE_RPS_PATH_UP: &str = "sys/class/drm/card0/gt/gt0/rps_up_threshold_pct";
const DEVICE_RPS_PATH_DOWN: &str = "sys/class/drm/card0/gt/gt0/rps_down_threshold_pct";
const DEVICE_RPS_DEFAULT_PATH_UP: &str =
    "sys/class/drm/card0/gt/gt0/.defaults/rps_up_threshold_pct";
const DEVICE_RPS_DEFAULT_PATH_DOWN: &str =
    "sys/class/drm/card0/gt/gt0/.defaults/rps_down_threshold_pct";

pub fn apply_borealis_tuning(root: &Path, enable: bool) -> Option<TuneSwappiness> {
    if enable {
        match intel_device::run_active_gpu_tuning(GPU_TUNING_POLLING_INTERVAL_MS) {
            Ok(_) => info!("Active GPU tuning running."),
            Err(e) => warn!("Active GPU tuning not set. {:?}", e),
        }
        let mut power_is_ac = false;
        match DirectoryPowerSourceProvider::new(root.to_path_buf()).get_power_source() {
            Ok(source) => power_is_ac = source == PowerSourceType::AC,
            Err(_) => warn!("Failed to get power state"),
        };

        if intel_device::is_intel_device(root.to_path_buf()) && power_is_ac {
            match set_rps_thresholds(root, GAMEMODE_RPS_UP, GAMEMODE_RPS_DOWN) {
                Ok(_) => {
                    info! {"Set RPS up/down freq to {:?}/{:?}",GAMEMODE_RPS_UP,GAMEMODE_RPS_DOWN}
                }
                Err(e) => {
                    warn! {"Failed to set RPS up/down values to {:?}/{:?}, {:?}"
                    ,GAMEMODE_RPS_UP,GAMEMODE_RPS_DOWN, e}
                }
            }
        }

        // Tuning CPU frequency.
        match intel_i7_or_above(Path::new(&root)) {
            Ok(res) => {
                if res && power_is_ac && double_min_freq(Path::new(&root)).is_err() {
                    warn! {"Failed to double scaling min freq"};
                }
            }
            Err(_) => {
                warn! {"Failed to check if device is Intel i7 or above"};
            }
        }
        // Tuning Transparent huge pages.
        if let Err(e) = set_thp(THPMode::Always) {
            warn! {"Failed to tune TPH: {:?}", e};
        }

        Some(TuneSwappiness {
            swappiness: TUNED_SWAPPINESS_VALUE,
        })
    } else {
        if intel_device::is_intel_device(root.to_path_buf()) {
            match reset_rps_thresholds(root) {
                Ok(_) => {
                    info! {"reset RPS up/down freq to defaults"}
                }
                Err(e) => {
                    warn! {"Failed to set RPS up/down values to defaults, due to {:?}"
                    ,e}
                }
            }
        }
        match intel_i7_or_above(Path::new(&root)) {
            Ok(res) => {
                if res && set_min_cpu_freq(Path::new(&root)).is_err() {
                    warn! {"Failed to set cpu min back to default values"};
                }
            }
            Err(_) => {
                warn! {"Failed to check if device is Intel i7 or above"};
            }
        }

        if let Err(e) = set_thp(THPMode::Default) {
            warn! {"Failed to set TPH to default: {:?}", e};
        }

        Some(TuneSwappiness {
            swappiness: DEFAULT_SWAPPINESS_VALUE,
        })
    }
}

fn reset_rps_thresholds(root: &Path) -> Result<()> {
    let mut default_up_rps = 95;
    if let Ok(val) = read_from_file(&root.join(DEVICE_RPS_DEFAULT_PATH_UP)) {
        default_up_rps = val;
    } else {
        warn!("Could not read rps up value.");
    };
    let mut default_down_rps = 85;
    if let Ok(val) = read_from_file(&root.join(DEVICE_RPS_DEFAULT_PATH_DOWN)) {
        default_down_rps = val;
    } else {
        warn!("Could not read rps down value.");
    };
    if set_rps_thresholds(root, default_up_rps, default_down_rps).is_err() {
        bail!("Failed to reset rps values to defaults.");
    };
    Ok(())
}

fn set_rps_thresholds(root: &Path, up: u64, down: u64) -> Result<()> {
    if root.join(DEVICE_RPS_DEFAULT_PATH_UP).exists()
        && root.join(DEVICE_RPS_DEFAULT_PATH_DOWN).exists()
    {
        std::fs::write(root.join(DEVICE_RPS_PATH_UP), up.to_string().as_bytes())?;
        std::fs::write(
            root.join(DEVICE_RPS_PATH_DOWN),
            down.to_string().as_bytes(),
        )?;
    } else {
        bail!("Failed to find path to RPS up/down nodes.")
    }
    Ok(())
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum THPMode {
    Default = 0,
    Always = 1,
}

// Enable THP tuning for devices with total memory > 4GB.
fn set_thp(mode: THPMode) -> Result<()> {
    const THP_MODE_PATH: &str = "/sys/kernel/mm/transparent_hugepage/enabled";
    let path = Path::new(THP_MODE_PATH);
    if path.exists() {
        static ENABLE_THP: Lazy<bool> = Lazy::new(|| match memory::get_meminfo() {
            Ok(meminfo) => meminfo.total > 9 * 1024 * 1024,
            Err(e) => {
                warn! {"Failed to validate device memory: {:?}", e};
                false
            }
        });

        if *ENABLE_THP {
            match mode {
                THPMode::Default => std::fs::write(THP_MODE_PATH, "madvise")?,
                THPMode::Always => std::fs::write(THP_MODE_PATH, "always")?,
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use tempfile::tempdir;

    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_modify_rps_value() {
        const RPS_DOWN_FREQ_PATH: &str = "sys/class/drm/card0/gt/gt0/rps_down_threshold_pct";
        const RPS_UP_FREQ_PATH: &str = "sys/class/drm/card0/gt/gt0/rps_up_threshold_pct";

        let root = tempdir().unwrap();
        let root_path = root.path();

        setup_mock_intel_gpu_dev_dirs(root_path);
        setup_mock_intel_gpu_files(root_path);

        let rps_down_path = root_path.join(RPS_DOWN_FREQ_PATH);
        let rps_up_path = root_path.join(RPS_UP_FREQ_PATH);

        set_rps_thresholds(root_path, 100, 25).unwrap();

        assert_eq!(read_from_file::<u64, _>(&rps_down_path).unwrap(), 25);
        assert_eq!(read_from_file::<u64, _>(&rps_up_path).unwrap(), 100);

        reset_rps_thresholds(root_path).unwrap();

        assert_eq!(read_from_file::<u64, _>(&rps_down_path).unwrap(), 95);
        assert_eq!(read_from_file::<u64, _>(&rps_up_path).unwrap(), 85);
    }
}
