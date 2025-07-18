// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::read_to_string;
use std::path::Path;
use std::path::PathBuf;
use std::str::FromStr;

use anyhow::Context;
use anyhow::Result;
use log::info;

use crate::arch;
use crate::common;
use crate::common::read_from_file;
use crate::common::BatterySaverMode;
use crate::common::FullscreenVideo;
use crate::common::GameMode;
use crate::common::RTCAudioActive;
use crate::common::ThermalState;
use crate::common::VmBootMode;
use crate::config::ConfigProvider;
use crate::config::CpuOfflinePreference;
use crate::config::Governor;
use crate::config::PowerPreferences;
use crate::config::PowerPreferencesType;
use crate::config::PowerSourceType;
use crate::cpu_utils::hotplug_cpus;
use crate::cpu_utils::write_to_cpu_policy_patterns;
use crate::cpu_utils::HotplugCpuAction;

const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";
const POWER_SUPPLY_ONLINE: &str = "online";
const POWER_SUPPLY_STATUS: &str = "status";
const GLOBAL_ONDEMAND_PATH: &str = "sys/devices/system/cpu/cpufreq/ondemand";

pub trait PowerSourceProvider {
    /// Returns the current power source of the system.
    fn get_power_source(&self) -> Result<PowerSourceType>;
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

#[derive(Clone, Debug)]
pub struct DirectoryPowerSourceProvider {
    root: PathBuf,
}

impl DirectoryPowerSourceProvider {
    pub fn new(root: PathBuf) -> Self {
        Self { root }
    }
}

impl PowerSourceProvider for DirectoryPowerSourceProvider {
    /// Iterates through all the power supplies in sysfs and looks for the `online` property.
    /// This indicates an external power source is connected (AC), but it doesn't necessarily
    /// mean it's powering the system. Tests will sometimes disable the charger to get power
    /// measurements. In order to determine if the charger is powering the system we need to
    /// look at the `status` property. If there is no charger connected and powering the system
    /// then we assume we are running off a battery (DC).
    fn get_power_source(&self) -> Result<PowerSourceType> {
        let path = self.root.join(POWER_SUPPLY_PATH);

        if !path.exists() {
            return Ok(PowerSourceType::DC);
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

            let online: u32 = read_from_file(&online_path)
                .with_context(|| format!("Error reading online from {}", online_path.display()))?;

            if online != 1 {
                continue;
            }

            let status_path = charger_path.path().join(POWER_SUPPLY_STATUS);

            if !status_path.exists() {
                continue;
            }

            let status_string = match read_to_string(&status_path) {
                Ok(data) => data,
                Err(e) => {
                    info!(
                        "Error reading status from {}, error: {}",
                        status_path.display(),
                        e
                    );
                    // On some boards, reading some power supply status may return ENODATA, it's an
                    // uncategorized Rust IO error kind. Continue checking other power supplies.
                    continue;
                }
            };

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

            return Ok(PowerSourceType::AC);
        }

        Ok(PowerSourceType::DC)
    }
}

pub trait PowerPreferencesManager {
    /// Chooses a [power preference](PowerPreferences) using the parameters and the
    /// system's current power source. It then applies it to the system.
    ///
    /// If more then one activity is active, the following priority list is used
    /// to determine which [power preference](PowerPreferences) to apply. If there is no
    /// power preference defined for an activity, the next activity in the list will be tried.
    ///
    /// 1) [Borealis Gaming](PowerPreferencesType::BorealisGaming)
    /// 2) [ARCVM Gaming](PowerPreferencesType::ArcvmGaming)
    /// 3) [WebRTC](PowerPreferencesType::WebRTC)
    /// 4) [Fullscreen Video](PowerPreferencesType::Fullscreen)
    /// 5) [VM boot Mode] (PowerPreferencesType::VmBoot)
    ///
    /// The [default](PowerPreferencesType::Default) preference will be applied when no
    /// activity is active.
    fn update_power_preferences(
        &self,
        rtc: common::RTCAudioActive,
        fullscreen: common::FullscreenVideo,
        game: common::GameMode,
        vmboot: common::VmBootMode,
        batterysaver: common::BatterySaverMode,
        thermalstate: common::ThermalState,
    ) -> Result<()>;
    fn get_root(&self) -> &Path;
}

#[derive(Clone, Debug)]
/// Applies [power preferences](PowerPreferences) to the system by writing to
/// the system's sysfs nodes.
///
/// This struct is using generics for the [ConfigProvider](ConfigProvider) and
/// [PowerSourceProvider] to make unit testing easier.
pub struct DirectoryPowerPreferencesManager<P: PowerSourceProvider> {
    root: PathBuf,
    config_provider: ConfigProvider,
    power_source_provider: P,
}

impl<P: PowerSourceProvider> DirectoryPowerPreferencesManager<P> {
    #[cfg(test)]
    pub fn new(root: &Path, config_provider: ConfigProvider, power_source_provider: P) -> Self {
        let root = root.to_path_buf();
        Self {
            root: root.to_path_buf(),
            config_provider,
            power_source_provider,
        }
    }

    // The global ondemand parameters are in /sys/devices/system/cpu/cpufreq/ondemand/.
    fn set_global_ondemand_governor_value(&self, attr: &str, value: u32) -> Result<()> {
        let path = self.root.join(GLOBAL_ONDEMAND_PATH).join(attr);

        let current_value_str = read_to_string(&path)
            .with_context(|| format!("Error reading ondemand parameter from {}", path.display()))?;
        let current_value = current_value_str.trim_end_matches('\n').parse::<u32>()?;

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
        write_to_cpu_policy_patterns(&pattern, &value.to_string())
    }

    fn set_scaling_governor(&self, new_governor: &str) -> Result<()> {
        const GOVERNOR_PATTERN: &str = "sys/devices/system/cpu/cpufreq/policy*/scaling_governor";
        let pattern = self
            .root
            .join(GOVERNOR_PATTERN)
            .to_str()
            .context("Cannot convert scaling_governor path to string")?
            .to_owned();

        write_to_cpu_policy_patterns(&pattern, new_governor)
    }

    fn apply_governor_preferences(&self, governor: Governor) -> Result<()> {
        self.set_scaling_governor(governor.name())?;

        if let Governor::Ondemand {
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

    fn apply_power_preferences(&self, preferences: PowerPreferences) -> Result<()> {
        arch::apply_platform_power_preferences(self.get_root(), &preferences)?;

        if let Some(governor) = preferences.governor {
            self.apply_governor_preferences(governor)?
        }

        Ok(())
    }

    fn apply_cpu_hotplug(&self, preferences: PowerPreferences) -> Result<()> {
        match preferences.cpu_offline {
            Some(CpuOfflinePreference::Smt { min_active_threads }) => hotplug_cpus(
                self.get_root(),
                HotplugCpuAction::OfflineSMT { min_active_threads },
            )?,
            Some(CpuOfflinePreference::Half { min_active_threads }) => hotplug_cpus(
                self.get_root(),
                HotplugCpuAction::OfflineHalf { min_active_threads },
            )?,
            Some(CpuOfflinePreference::SmallCore { min_active_threads }) => hotplug_cpus(
                self.get_root(),
                HotplugCpuAction::OfflineSmallCore { min_active_threads },
            )?,
            None => {
                hotplug_cpus(self.get_root(), HotplugCpuAction::OnlineAll)?;
            }
        }

        Ok(())
    }

    fn apply_cpufreq_boost(&self, preferences: PowerPreferences) -> Result<()> {
        let boost_value = if preferences.cpufreq_disable_boost {
            // Disable boost by writing '0'.
            "0"
        } else {
            // Enable boost by writing '1'.
            "1"
        };

        const CPUFREQ_BOOST_PATH: &str = "sys/devices/system/cpu/cpufreq/boost";
        let cpufreq_boost_path = self.root.join(CPUFREQ_BOOST_PATH);
        if cpufreq_boost_path.exists() {
            let boost = std::fs::read_to_string(&cpufreq_boost_path)?;

            if boost.trim() != boost_value {
                std::fs::write(&cpufreq_boost_path, boost_value).with_context(|| {
                    format!(
                        "Error writing {} to {}",
                        boost_value,
                        cpufreq_boost_path.display()
                    )
                })?;

                info!("Updating cpufreq boost {}", boost_value);
            }
        }

        Ok(())
    }
}

impl<P: PowerSourceProvider> PowerPreferencesManager for DirectoryPowerPreferencesManager<P> {
    fn update_power_preferences(
        &self,
        rtc: RTCAudioActive,
        fullscreen: FullscreenVideo,
        game: GameMode,
        vmboot: VmBootMode,
        batterysaver: BatterySaverMode,
        thermalstate: ThermalState,
    ) -> Result<()> {
        let mut preferences: Option<PowerPreferences> = None;

        let power_source = self.power_source_provider.get_power_source()?;

        info!("Power source {:?}", power_source);
        info!("Thermal state {:?}", thermalstate);

        if batterysaver == BatterySaverMode::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::BatterySaver)?;
        } else if thermalstate == ThermalState::Stress {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::ThermalStress)?;
        } else if game == GameMode::Borealis {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::BorealisGaming)?;
        } else if game == GameMode::Arc {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::ArcvmGaming)?;
        }

        if preferences.is_none() && rtc == RTCAudioActive::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::WebRTC)?;
        }

        if preferences.is_none() && fullscreen == FullscreenVideo::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::Fullscreen)?;
        }

        if preferences.is_none() && vmboot == VmBootMode::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::VmBoot)?;
        }

        if preferences.is_none() {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, PowerPreferencesType::Default)?;
        }

        // For non-dynamic EPP, it may need default EPP
        let mut need_default_epp = true;
        if let Some(preferences) = preferences {
            self.apply_cpu_hotplug(preferences)?;
            self.apply_power_preferences(preferences)?;
            self.apply_cpufreq_boost(preferences)?;
            if preferences.epp.is_some() {
                need_default_epp = false;
            }
        }

        arch::apply_platform_power_settings(
            self.get_root(),
            power_source,
            rtc,
            fullscreen,
            batterysaver,
            need_default_epp, /* For x86 non-dynamic EPP */
        )?;

        Ok(())
    }

    fn get_root(&self) -> &Path {
        self.root.as_path()
    }
}

pub fn new_directory_power_preferences_manager(
    root: &Path,
    config_provider: ConfigProvider,
) -> DirectoryPowerPreferencesManager<DirectoryPowerSourceProvider> {
    let root = root.to_path_buf();
    DirectoryPowerPreferencesManager {
        root: root.clone(),
        config_provider,
        power_source_provider: DirectoryPowerSourceProvider::new(root),
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::Path;

    use tempfile::tempdir;

    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_parse_power_supply_status() {
        assert_eq!(
            PowerSupplyStatus::from_str("Unknown\n").unwrap(),
            PowerSupplyStatus::Unknown
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Charging\n").unwrap(),
            PowerSupplyStatus::Charging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Discharging\n").unwrap(),
            PowerSupplyStatus::Discharging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Not charging\n").unwrap(),
            PowerSupplyStatus::NotCharging
        );
        assert_eq!(
            PowerSupplyStatus::from_str("Full\n").unwrap(),
            PowerSupplyStatus::Full
        );

        assert!(PowerSupplyStatus::from_str("").is_err());
        assert!(PowerSupplyStatus::from_str("abc").is_err());
    }

    #[test]
    fn test_power_source_provider_empty_root() {
        let root = tempdir().unwrap();

        let provider = DirectoryPowerSourceProvider {
            root: root.path().to_path_buf(),
        };

        let power_source = provider.get_power_source().unwrap();

        assert_eq!(power_source, PowerSourceType::DC);
    }

    const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";

    #[test]
    fn test_power_source_provider_empty_path() {
        let root = tempdir().unwrap();

        let path = root.path().join(POWER_SUPPLY_PATH);
        fs::create_dir_all(path).unwrap();

        let provider = DirectoryPowerSourceProvider {
            root: root.path().to_path_buf(),
        };

        let power_source = provider.get_power_source().unwrap();

        assert_eq!(power_source, PowerSourceType::DC);
    }

    /// Tests that the `DirectoryPowerSourceProvider` can parse the charger sysfs
    /// `online` and `status` attributes.
    #[test]
    fn test_power_source_provider_disconnected_then_connected() {
        let root = tempdir().unwrap();

        let path = root.path().join(POWER_SUPPLY_PATH);
        fs::create_dir_all(&path).unwrap();

        let provider = DirectoryPowerSourceProvider {
            root: root.path().to_path_buf(),
        };

        let charger = path.join("charger-1");
        fs::create_dir_all(&charger).unwrap();
        let online = charger.join("online");

        fs::write(&online, b"0").unwrap();
        let power_source = provider.get_power_source().unwrap();
        assert_eq!(power_source, PowerSourceType::DC);

        let status = charger.join("status");
        fs::write(&online, b"1").unwrap();
        fs::write(&status, b"Charging\n").unwrap();
        let power_source = provider.get_power_source().unwrap();
        assert_eq!(power_source, PowerSourceType::AC);

        fs::write(&online, b"1").unwrap();
        fs::write(&status, b"Not Charging\n").unwrap();
        let power_source = provider.get_power_source().unwrap();
        assert_eq!(power_source, PowerSourceType::DC);
    }

    fn write_global_powersave_bias(root: &Path, value: u32) -> Result<()> {
        let ondemand_path = root.join("sys/devices/system/cpu/cpufreq/ondemand");
        fs::create_dir_all(&ondemand_path)?;

        std::fs::write(
            ondemand_path.join("powersave_bias"),
            value.to_string() + "\n",
        )?;

        Ok(())
    }

    fn read_global_powersave_bias(root: &Path) -> Result<String> {
        let powersave_bias_path = root
            .join("sys/devices/system/cpu/cpufreq/ondemand")
            .join("powersave_bias");

        let mut powersave_bias = std::fs::read_to_string(powersave_bias_path)?;
        if powersave_bias.ends_with('\n') {
            powersave_bias.pop();
        }

        Ok(powersave_bias)
    }

    fn write_global_sampling_rate(root: &Path, value: u32) -> Result<()> {
        let ondemand_path = root.join("sys/devices/system/cpu/cpufreq/ondemand");
        fs::create_dir_all(&ondemand_path)?;

        std::fs::write(ondemand_path.join("sampling_rate"), value.to_string())?;

        Ok(())
    }

    fn read_global_sampling_rate(root: &Path) -> Result<String> {
        let sampling_rate_path = root
            .join("sys/devices/system/cpu/cpufreq/ondemand")
            .join("sampling_rate");

        let mut sampling_rate = std::fs::read_to_string(sampling_rate_path)?;
        if sampling_rate.ends_with('\n') {
            sampling_rate.pop();
        }

        Ok(sampling_rate)
    }

    fn read_global_cpufreq_boost(root: &Path) -> Result<String> {
        let cpufreq_boost_path = root.join("sys/devices/system/cpu/cpufreq/boost");

        let mut boost = std::fs::read_to_string(cpufreq_boost_path)?;
        boost = boost.trim().to_string();

        Ok(boost)
    }

    fn write_global_cpufreq_boost(root: &Path, value: u32) -> Result<()> {
        let cpufreq_boost_path = root.join("sys/devices/system/cpu/cpufreq");
        fs::create_dir_all(&cpufreq_boost_path)?;

        std::fs::write(cpufreq_boost_path.join("boost"), value.to_string())?;

        Ok(())
    }

    #[test]
    fn test_power_update_power_preferences_wrong_governor() {
        let root = tempdir().unwrap();

        test_write_cpuset_root_cpus(root.path(), "0-3");
        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let fake_config = FakeConfig::new();
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        // We shouldn't have written anything.
        let powersave_bias = read_global_powersave_bias(root.path());
        assert!(powersave_bias.is_err());
    }

    #[test]
    fn test_power_update_power_preferences_none() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let fake_config = FakeConfig::new();
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "0");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "2000");
    }

    #[test]
    fn test_power_update_power_preferences_default_ac() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        write_global_cpufreq_boost(root.path(), 1).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::Default,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "16000");

        let cpufreq_boost = read_global_cpufreq_boost(root.path()).unwrap();
        assert_eq!(cpufreq_boost, "1");
    }

    #[test]
    fn test_power_update_power_preferences_default_dc() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        write_global_cpufreq_boost(root.path(), 1).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::DC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::DC,
            PowerPreferencesType::Default,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: None,
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: true,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "2000");

        let cpufreq_boost = read_global_cpufreq_boost(root.path()).unwrap();
        assert_eq!(cpufreq_boost, "0");
    }

    #[test]
    fn test_power_update_power_preferences_default_rtc_active() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::Default,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(4000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Active,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "4000");
    }

    #[test]
    fn test_power_update_power_preferences_rtc_active() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::WebRTC,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Active,
                common::FullscreenVideo::Inactive,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "16000");
    }

    /// Tests default battery saver mode
    #[test]
    fn test_apply_hotplug_cpus() {
        struct Test<'a> {
            cpus: &'a str,
            cluster1_state: [&'a str; 2],
            cluster2_state: [&'a str; 2],
            cluster1_freq: [u32; 2],
            cluster2_freq: [u32; 2],
            preferences: PowerPreferences,
            smt_offlined: bool,
            smt_orig_state: &'a str,
            cluster1_expected_state: [&'a str; 2],
            cluster2_expected_state: [&'a str; 2],
            smt_expected_state: &'a str,
        }

        let tests = [
            // Test offline small core
            Test {
                cpus: "0-3",
                cluster1_state: ["1"; 2],
                cluster2_state: ["1"; 2],
                cluster1_freq: [2400000; 2],
                cluster2_freq: [1800000; 2],
                preferences: PowerPreferences {
                    governor: Some(Governor::Conservative),
                    epp: None,
                    cpu_offline: Some(CpuOfflinePreference::SmallCore {
                        min_active_threads: 2,
                    }),
                    cpufreq_disable_boost: false,
                },
                smt_offlined: false,
                smt_orig_state: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "",
            },
            // Test offline SMT
            Test {
                cpus: "0-3",
                cluster1_state: ["1"; 2],
                cluster2_state: ["1"; 2],
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                preferences: PowerPreferences {
                    governor: None,
                    epp: None,
                    cpu_offline: Some(CpuOfflinePreference::Smt {
                        min_active_threads: 2,
                    }),
                    cpufreq_disable_boost: false,
                },
                smt_offlined: true,
                smt_orig_state: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "off",
            },
            // Test offline half
            Test {
                cpus: "0-3",
                cluster1_state: ["1"; 2],
                cluster2_state: ["1"; 2],
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                preferences: PowerPreferences {
                    governor: Some(Governor::Conservative),
                    epp: None,
                    cpu_offline: Some(CpuOfflinePreference::Half {
                        min_active_threads: 2,
                    }),
                    cpufreq_disable_boost: false,
                },
                smt_offlined: false,
                smt_orig_state: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "off",
            },
            // Test online all
            Test {
                cpus: "0-3",
                cluster1_state: ["1"; 2],
                cluster2_state: ["0"; 2],
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                preferences: PowerPreferences {
                    governor: None,
                    epp: None,
                    cpu_offline: None,
                    cpufreq_disable_boost: false,
                },
                smt_offlined: false,
                smt_orig_state: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["1"; 2],
                smt_expected_state: "off",
            },
        ];

        for test in tests {
            //Setup
            let temp_dir = tempdir().unwrap();
            let root = temp_dir.path();
            let fake_config = FakeConfig::new();
            let config_provider = fake_config.provider();
            let manager = DirectoryPowerPreferencesManager {
                root: root.to_path_buf(),
                config_provider,
                power_source_provider: FakePowerSourceProvider {
                    power_source: PowerSourceType::DC,
                },
            };
            test_write_cpuset_root_cpus(root, test.cpus);
            test_write_smt_control(root, test.smt_orig_state);
            // Setup core cpus list for two physical cores and two virtual cores
            test_write_core_cpus_list(root, 0, "0,2");
            test_write_core_cpus_list(root, 1, "1,3");
            test_write_core_cpus_list(root, 2, "0,2");
            test_write_core_cpus_list(root, 3, "1,3");

            for (i, freq) in test.cluster1_freq.iter().enumerate() {
                test_write_online_cpu(root, i.try_into().unwrap(), test.cluster1_state[i]);
                test_write_cpu_max_freq(root, i.try_into().unwrap(), *freq);
            }
            for (i, freq) in test.cluster2_freq.iter().enumerate() {
                test_write_online_cpu(
                    root,
                    (test.cluster1_freq.len() + i).try_into().unwrap(),
                    test.cluster2_state[i],
                );
                test_write_cpu_max_freq(
                    root,
                    (test.cluster1_freq.len() + i).try_into().unwrap(),
                    *freq,
                );
            }

            // Call function to test
            manager.apply_cpu_hotplug(test.preferences).unwrap();

            // Check result.
            if test.smt_offlined {
                // The mock sysfs cannot offline the SMT CPUs, here to check the smt control state
                test_check_smt_control(root, test.smt_expected_state);
                continue;
            }

            for (i, state) in test.cluster1_expected_state.iter().enumerate() {
                test_check_online_cpu(root, i.try_into().unwrap(), state);
            }

            for (i, state) in test.cluster2_expected_state.iter().enumerate() {
                test_check_online_cpu(
                    root,
                    (test.cluster1_expected_state.len() + i).try_into().unwrap(),
                    state,
                );
            }
        }
    }

    #[test]
    fn test_power_update_power_preferences_fullscreen_active() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::Fullscreen,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Active,
                common::GameMode::Off,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "16000");
    }

    #[test]
    fn test_power_update_power_preferences_borealis_gaming_active() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::BorealisGaming,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };

        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Borealis,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "16000");
    }

    #[test]
    fn test_power_update_power_preferences_arcvm_gaming_active() {
        let root = tempdir().unwrap();

        write_global_powersave_bias(root.path(), 0).unwrap();
        write_global_sampling_rate(root.path(), 2000).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::ArcvmGaming,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.path().to_path_buf(),
            config_provider,
            power_source_provider,
        };
        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Arc,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();

        let powersave_bias = read_global_powersave_bias(root.path()).unwrap();
        assert_eq!(powersave_bias, "200");

        let sampling_rate = read_global_sampling_rate(root.path()).unwrap();
        assert_eq!(sampling_rate, "16000");
    }

    #[test]
    fn test_per_policy_ondemand_governor() {
        let temp_dir = tempdir().unwrap();
        let root = temp_dir.path();

        const INIT_POWERSAVE_BIAS: u32 = 0;
        const INIT_SAMPLING_RATE: u32 = 2000;
        const CONFIG_POWERSAVE_BIAS: u32 = 200;
        const CONFIG_SAMPLING_RATE: u32 = 16000;

        let ondemand = Governor::Ondemand {
            powersave_bias: INIT_POWERSAVE_BIAS,
            sampling_rate: Some(INIT_SAMPLING_RATE),
        };
        let policy0 = PolicyConfigs {
            policy_path: TEST_CPUFREQ_POLICIES[0],
            governor: &ondemand,
            affected_cpus: "0",
        };
        let policy1 = PolicyConfigs {
            policy_path: TEST_CPUFREQ_POLICIES[1],
            governor: &ondemand,
            affected_cpus: "1",
        };
        let policies = vec![policy0, policy1];
        write_per_policy_scaling_governor(root, policies);
        write_per_policy_powersave_bias(root, INIT_POWERSAVE_BIAS);
        write_per_policy_sampling_rate(root, INIT_SAMPLING_RATE);
        test_write_cpuset_root_cpus(root, "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::ArcvmGaming,
            &PowerPreferences {
                governor: Some(Governor::Ondemand {
                    powersave_bias: CONFIG_POWERSAVE_BIAS,
                    sampling_rate: Some(CONFIG_SAMPLING_RATE),
                }),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager = DirectoryPowerPreferencesManager {
            root: root.to_path_buf(),
            config_provider,
            power_source_provider,
        };
        manager
            .update_power_preferences(
                common::RTCAudioActive::Inactive,
                common::FullscreenVideo::Inactive,
                common::GameMode::Arc,
                common::VmBootMode::Inactive,
                common::BatterySaverMode::Inactive,
                common::ThermalState::Normal,
            )
            .unwrap();
        check_per_policy_scaling_governor(root, vec![ondemand, ondemand]);
        check_per_policy_powersave_bias(root, CONFIG_POWERSAVE_BIAS);
        check_per_policy_sampling_rate(root, CONFIG_SAMPLING_RATE);
    }

    #[test]
    fn test_scaling_governors() {
        let temp_dir = tempdir().unwrap();
        let root = temp_dir.path();

        test_write_cpuset_root_cpus(root, "0-3");
        const INIT_POWERSAVE_BIAS: u32 = 0;
        const INIT_SAMPLING_RATE: u32 = 2000;

        let ondemand = Governor::Ondemand {
            powersave_bias: INIT_POWERSAVE_BIAS,
            sampling_rate: Some(INIT_SAMPLING_RATE),
        };
        let policy0 = PolicyConfigs {
            policy_path: TEST_CPUFREQ_POLICIES[0],
            governor: &ondemand,
            affected_cpus: AFFECTED_CPU0,
        };
        let policy1 = PolicyConfigs {
            policy_path: TEST_CPUFREQ_POLICIES[1],
            governor: &ondemand,
            affected_cpus: AFFECTED_CPU1,
        };
        let policies = vec![policy0, policy1];
        write_per_policy_scaling_governor(root, policies);

        let governors = [
            Governor::Conservative,
            Governor::Performance,
            Governor::Powersave,
            Governor::Schedutil,
            Governor::Userspace,
        ];

        for governor in governors {
            let power_source_provider = FakePowerSourceProvider {
                power_source: PowerSourceType::AC,
            };
            let mut fake_config = FakeConfig::new();
            fake_config.write_power_preference(
                PowerSourceType::AC,
                PowerPreferencesType::ArcvmGaming,
                &PowerPreferences {
                    governor: Some(governor),
                    epp: None,
                    cpu_offline: None,
                    cpufreq_disable_boost: false,
                },
            );
            let config_provider = fake_config.provider();
            let manager = DirectoryPowerPreferencesManager {
                root: root.to_path_buf(),
                config_provider,
                power_source_provider,
            };

            manager
                .update_power_preferences(
                    common::RTCAudioActive::Inactive,
                    common::FullscreenVideo::Inactive,
                    common::GameMode::Arc,
                    common::VmBootMode::Inactive,
                    common::BatterySaverMode::Inactive,
                    common::ThermalState::Normal,
                )
                .unwrap();

            check_per_policy_scaling_governor(root, vec![governor, governor]);
        }
    }
}
