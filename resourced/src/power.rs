// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::read_to_string;
use std::path::PathBuf;
use std::str::FromStr;

use anyhow::{Context, Result};
use glob::glob;
use sys_util::info;

use crate::common;
use crate::common::{FullscreenVideo, GameMode, RTCAudioActive};
use crate::config;

const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";
const POWER_SUPPLY_ONLINE: &str = "online";
const POWER_SUPPLY_STATUS: &str = "status";
const GLOBAL_ONDEMAND_PATH: &str = "sys/devices/system/cpu/cpufreq/ondemand";

pub trait PowerSourceProvider {
    /// Returns the current power source of the system.
    fn get_power_source(&self) -> Result<config::PowerSourceType>;
}

/// See the `POWER_SUPPLY_STATUS_` enum in the linux kernel.
/// These values are intended to describe the battery status. They are also used
/// to describe the charger status, which adds a little bit of confusion. A
/// charger will only return `Charging` or `NotCharging`.
#[derive(Copy, Clone, Debug, PartialEq)]
enum PowerSupplyStatus {
    Unknown,
    Charging,
    Discharging,
    NotCharging,
    Full,
}

impl FromStr for PowerSupplyStatus {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim_end();

        match s {
            "Unknown" => Ok(PowerSupplyStatus::Unknown),
            "Charging" => Ok(PowerSupplyStatus::Charging),
            "Discharging" => Ok(PowerSupplyStatus::Discharging),
            "Not charging" => Ok(PowerSupplyStatus::NotCharging),
            "Full" => Ok(PowerSupplyStatus::Full),
            _ => anyhow::bail!("Unknown Power Supply Status: '{}'", s),
        }
    }
}

#[derive(Debug)]
pub struct DirectoryPowerSourceProvider {
    pub root: PathBuf,
}

impl PowerSourceProvider for DirectoryPowerSourceProvider {
    /// Iterates through all the power supplies in sysfs and looks for the `online` property.
    /// This indicates an external power source is connected (AC), but it doesn't necessarily
    /// mean it's powering the system. Tests will sometimes disable the charger to get power
    /// measurements. In order to determine if the charger is powering the system we need to
    /// look at the `status` property. If there is no charger connected and powering the system
    /// then we assume we are running off a battery (DC).
    fn get_power_source(&self) -> Result<config::PowerSourceType> {
        let path = self.root.join(POWER_SUPPLY_PATH);

        if !path.exists() {
            return Ok(config::PowerSourceType::DC);
        }

        let dirs = path
            .read_dir()
            .with_context(|| format!("Failed to enumerate power supplies in {}", path.display()))?;

        for result in dirs {
            let charger_path = result?;

            let online_path = charger_path.path().join(POWER_SUPPLY_ONLINE);

            if !online_path.exists() {
                continue;
            }

            let online = common::read_file_to_u64(&online_path)
                .with_context(|| format!("Error reading online from {}", online_path.display()))?
                as u32;

            if online != 1 {
                continue;
            }

            let status_path = charger_path.path().join(POWER_SUPPLY_STATUS);

            if !status_path.exists() {
                continue;
            }

            let status_string = read_to_string(&status_path)
                .with_context(|| format!("Error reading status from {}", status_path.display()))?;

            let status_result = PowerSupplyStatus::from_str(&status_string);

            let status = match status_result {
                Err(_) => {
                    info!(
                        "Failure parsing '{}' from {}",
                        status_string,
                        status_path.display()
                    );
                    continue;
                }
                Ok(status) => status,
            };

            if status != PowerSupplyStatus::Charging {
                continue;
            }

            return Ok(config::PowerSourceType::AC);
        }

        Ok(config::PowerSourceType::DC)
    }
}

pub trait PowerPreferencesManager {
    /// Chooses a [power preference](config::PowerPreferences) using the parameters and the
    /// system's current power source. It then applies it to the system.
    ///
    /// If more then one activity is active, the following priority list is used
    /// to determine which [power preference](config::PowerPreferences) to apply. If there is no
    /// power preference defined for an activity, the next activity in the list will be tried.
    ///
    /// 1) [Borealis Gaming](config::PowerPreferencesType::BorealisGaming)
    /// 2) [ARCVM Gaming](config::PowerPreferencesType::ArcvmGaming)
    /// 3) [WebRTC](config::PowerPreferencesType::WebRTC)
    /// 4) [Fullscreen Video](config::PowerPreferencesType::Fullscreen)
    ///
    /// The [default](config::PowerPreferencesType::Default) preference will be applied when no
    /// activity is active.
    fn update_power_preferences(
        &self,
        rtc: common::RTCAudioActive,
        fullscreen: common::FullscreenVideo,
        game: common::GameMode,
    ) -> Result<()>;
}

fn write_to_path_patterns(pattern: &str, new_value: &str) -> Result<()> {
    for entry in glob(pattern)? {
        let path = entry?;
        let current_value = read_to_string(&path)
            .with_context(|| format!("Error reading attribute from {}", path.display()))?;
        if current_value != new_value {
            std::fs::write(&path, new_value).with_context(|| {
                format!(
                    "Failed to set attribute to {}, new value: {}",
                    path.display(),
                    new_value
                )
            })?;
        }
    }

    Ok(())
}

#[derive(Debug)]
/// Applies [power preferences](config::PowerPreferences) to the system by writing to
/// the system's sysfs nodes.
///
/// This struct is using generics for the [ConfigProvider](config::ConfigProvider) and
/// [PowerSourceProvider] to make unit testing easier.
pub struct DirectoryPowerPreferencesManager<C: config::ConfigProvider, P: PowerSourceProvider> {
    pub root: PathBuf,
    pub config_provider: C,
    pub power_source_provider: P,
}

impl<C: config::ConfigProvider, P: PowerSourceProvider> DirectoryPowerPreferencesManager<C, P> {
    // The global ondemand parameters are in /sys/devices/system/cpu/cpufreq/ondemand/.
    fn set_global_ondemand_governor_value(&self, attr: &str, value: u32) -> Result<()> {
        let path = self.root.join(GLOBAL_ONDEMAND_PATH).join(attr);

        let current_value_str = read_to_string(&path)
            .with_context(|| format!("Error reading ondemand parameter from {}", path.display()))?;
        let current_value = current_value_str.parse::<u32>()?;

        // Check current value before writing to avoid permission error when the new value and
        // current value are the same but resourced didn't own the parameter file.
        if current_value != value {
            std::fs::write(&path, value.to_string()).with_context(|| {
                format!("Error writing {} {} to {}", attr, value, path.display())
            })?;

            info!("Updating ondemand {} to {}", attr, value);
        }

        Ok(())
    }

    // The per-policy ondemand parameters are in /sys/devices/system/cpu/cpufreq/policy*/ondemand/.
    fn set_per_policy_ondemand_governor_value(&self, attr: &str, value: u32) -> Result<()> {
        const ONDEMAND_PATTERN: &str = "sys/devices/system/cpu/cpufreq/policy*/ondemand";
        let pattern = self
            .root
            .join(ONDEMAND_PATTERN)
            .join(attr)
            .to_str()
            .context("Cannot convert ondemand path to string")?
            .to_owned();
        write_to_path_patterns(&pattern, &value.to_string())
    }

    fn set_scaling_governor(&self, new_governor: &str) -> Result<()> {
        const GOVERNOR_PATTERN: &str = "sys/devices/system/cpu/cpufreq/policy*/scaling_governor";
        let pattern = self
            .root
            .join(GOVERNOR_PATTERN)
            .to_str()
            .context("Cannot convert scaling_governor path to string")?
            .to_owned();

        write_to_path_patterns(&pattern, new_governor)
    }

    fn apply_governor_preferences(&self, governor: config::Governor) -> Result<()> {
        self.set_scaling_governor(governor.to_name())?;

        if let config::Governor::Ondemand {
            powersave_bias,
            sampling_rate,
        } = governor
        {
            let global_path = self.root.join(GLOBAL_ONDEMAND_PATH);

            // There are 2 use cases now:
            // 1. on guybrush, the scaling_governor is always ondemand, the ondemand directory is
            //    chowned to resourced so resourced can set the ondemand parameters.
            // 2. on herobrine, resourced only changes the scaling_governor, so the permission of
            //    the new governor's sysfs nodes doesn't matter.
            // TODO: support changing both the scaling_governor and the governor's parameters.

            // The ondemand tunable could be global (system-wide) or per-policy, depending on the
            // scaling driver in use [1]. The global ondemand tunable is in
            // /sys/devices/system/cpu/cpufreq/ondemand. The per-policy ondemand tunable is in
            // /sys/devices/system/cpu/cpufreq/policy*/ondemand.
            //
            // [1]: https://www.kernel.org/doc/html/latest/admin-guide/pm/cpufreq.html
            if global_path.exists() {
                self.set_global_ondemand_governor_value("powersave_bias", powersave_bias)?;

                if let Some(sampling_rate) = sampling_rate {
                    self.set_global_ondemand_governor_value("sampling_rate", sampling_rate)?;
                }
            } else {
                self.set_per_policy_ondemand_governor_value("powersave_bias", powersave_bias)?;
                if let Some(sampling_rate) = sampling_rate {
                    self.set_per_policy_ondemand_governor_value("sampling_rate", sampling_rate)?;
                }
            }
        }

        Ok(())
    }

    fn apply_power_preferences(&self, preferences: config::PowerPreferences) -> Result<()> {
        if let Some(governor) = preferences.governor {
            self.apply_governor_preferences(governor)?
        }

        Ok(())
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

impl<C: config::ConfigProvider, P: PowerSourceProvider> PowerPreferencesManager
    for DirectoryPowerPreferencesManager<C, P>
{
    fn update_power_preferences(
        &self,
        rtc: RTCAudioActive,
        fullscreen: FullscreenVideo,
        game: GameMode,
    ) -> Result<()> {
        let mut preferences: Option<config::PowerPreferences> = None;

        let power_source = self.power_source_provider.get_power_source()?;

        info!("Power source {:?}", power_source);

        if game == GameMode::Borealis {
            preferences = self.config_provider.read_power_preferences(
                power_source,
                config::PowerPreferencesType::BorealisGaming,
            )?;
        } else if game == GameMode::Arc {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::ArcvmGaming)?;
        }

        if preferences.is_none() && rtc == RTCAudioActive::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::WebRTC)?;
        }

        if preferences.is_none() && fullscreen == FullscreenVideo::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::Fullscreen)?;
        }

        if preferences.is_none() {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::Default)?;
        }

        if let Some(preferences) = preferences {
            self.apply_power_preferences(preferences)?
        }

        if let Some(root) = self.root.to_str() {
            /* TODO(b/233359053): These values should be migrated to chromeos-config */
            if rtc == RTCAudioActive::Active || fullscreen == FullscreenVideo::Active {
                set_epp(root, "179")?; // Set EPP to 70%
            } else if rtc != RTCAudioActive::Active && fullscreen != FullscreenVideo::Active {
                set_epp(root, "balance_performance")?; // Default EPP
            }
        } else {
            info!("Converting root path failed: {}", self.root.display());
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_power_supply_status() -> anyhow::Result<()> {
        assert_eq!(
            PowerSupplyStatus::from_str("Unknown\n")?,
            PowerSupplyStatus::Unknown
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Charging\n")?,
            PowerSupplyStatus::Charging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Discharging\n")?,
            PowerSupplyStatus::Discharging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Not charging\n")?,
            PowerSupplyStatus::NotCharging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Full\n")?,
            PowerSupplyStatus::Full
        );

        assert!(PowerSupplyStatus::from_str("").is_err());
        assert!(PowerSupplyStatus::from_str("abc").is_err());

        Ok(())
    }
}
