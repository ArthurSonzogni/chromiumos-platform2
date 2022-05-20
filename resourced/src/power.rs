// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use anyhow::{Context, Result};
use sys_util::info;

use crate::common;
use crate::config;

const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";
const POWER_SUPPLY_ONLINE: &str = "online";
const ONDEMAND_PATH: &str = "sys/devices/system/cpu/cpufreq/ondemand";

pub trait PowerSourceProvider {
    /// Returns the current power source of the system.
    fn get_power_source(&self) -> Result<config::PowerSourceType>;
}

#[derive(Debug)]
pub struct DirectoryPowerSourceProvider {
    pub root: PathBuf,
}

impl PowerSourceProvider for DirectoryPowerSourceProvider {
    /// Iterates through all the power supplies in sysfs and looks for the `online` property.
    /// If online == 1, then the system is connected to external power (AC), otherwise the
    /// system is powered via battery (DC).
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

            if online == 1 {
                return Ok(config::PowerSourceType::AC);
            }
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
    /// 1) [Gaming](config::PowerPreferencesType::Gaming)
    /// 2) [WebRTC](config::PowerPreferencesType::WebRTC)
    /// 3) [Fullscreen Video](config::PowerPreferencesType::Fullscreen)
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
    fn set_ondemand_power_bias(&self, value: u32) -> Result<()> {
        let path = self.root.join(ONDEMAND_PATH).join("powersave_bias");

        std::fs::write(&path, value.to_string()).with_context(|| {
            format!(
                "Error writing powersave_bias {} to {}",
                value,
                path.display()
            )
        })?;

        info!("Updating ondemand powersave_bias to {}", value);

        Ok(())
    }

    fn apply_governor_preferences(&self, governor: config::Governor) -> Result<()> {
        match governor {
            config::Governor::OndemandGovernor { powersave_bias } => {
                let path = self.root.join(ONDEMAND_PATH);

                if !path.exists() {
                    // We don't support switching the current governor. In theory we could,
                    // but we would need to figure out how to grant resourced write permissions
                    // to the new governor's sysfs nodes. Since we currently only support one
                    // governor in the config, we just print a message.
                    info!("ondemand governor is not active, not applying preferences.");
                    return Ok(());
                }

                self.set_ondemand_power_bias(powersave_bias)?
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

impl<'a, C: config::ConfigProvider, P: PowerSourceProvider> PowerPreferencesManager
    for DirectoryPowerPreferencesManager<C, P>
{
    fn update_power_preferences(
        &self,
        rtc: common::RTCAudioActive,
        fullscreen: common::FullscreenVideo,
        game: common::GameMode,
    ) -> Result<()> {
        let mut preferences: Option<config::PowerPreferences> = None;

        let power_source = self.power_source_provider.get_power_source()?;

        info!("Power source {:?}", power_source);

        if game != common::GameMode::Off {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::Gaming)?;
        }

        if preferences.is_none() && rtc == common::RTCAudioActive::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::WebRTC)?;
        }

        if preferences.is_none() && fullscreen == common::FullscreenVideo::Active {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::Fullscreen)?;
        }

        if preferences.is_none() {
            preferences = self
                .config_provider
                .read_power_preferences(power_source, config::PowerPreferencesType::Default)?;
        }

        match preferences {
            Some(preferences) => self.apply_power_preferences(preferences),
            None => Ok(()),
        }
    }
}
