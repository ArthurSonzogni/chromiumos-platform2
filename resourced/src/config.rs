// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use std::fs;
use std::fs::DirEntry;
use std::path::Path;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;

use crate::common::read_file_to_u64;

const CHROMEOS_CONFIG_PATH: &str = "run/chromeos-config/v1";
const RESOURCE_CONFIG_DIR: &str = "resource";
const DEFUALT_MIN_ACTIVE_THREADS: u32 = 2;

pub trait FromDir {
    // The return type is Result<Option<Self>> so parse_config_from_path() don't have to
    // parse the result.
    fn from_dir(dir: DirEntry) -> Result<Option<Self>>
    where
        Self: Sized;
}

/* TODO: Can we use `rust-protobuf` to generate all the structs? */

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Governor {
    Conservative,
    Ondemand {
        powersave_bias: u32,
        sampling_rate: Option<u32>,
    },
    Performance,
    Powersave,
    Schedutil,
    Userspace,
}

impl Governor {
    pub fn name(&self) -> &'static str {
        match self {
            Governor::Conservative => "conservative",
            Governor::Ondemand { .. } => "ondemand",
            Governor::Performance => "performance",
            Governor::Powersave => "powersave",
            Governor::Schedutil => "schedutil",
            Governor::Userspace => "userspace",
        }
    }
}

impl FromDir for Governor {
    fn from_dir(dir: DirEntry) -> Result<Option<Governor>> {
        match dir.file_name().to_str() {
            Some("conservative") => Ok(Some(Governor::Conservative)),
            Some("ondemand") => Ok(Some(parse_ondemand_governor(&dir.path())?)),
            Some("performance") => Ok(Some(Governor::Performance)),
            Some("powersave") => Ok(Some(Governor::Powersave)),
            Some("schedutil") => Ok(Some(Governor::Schedutil)),
            Some("userspace") => Ok(Some(Governor::Userspace)),
            _ => bail!("Unknown governor {:?}!", dir.file_name()),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EnergyPerformancePreference {
    Default,
    Performance,
    BalancePerformance,
    BalancePower,
    Power,
}

impl EnergyPerformancePreference {
    pub fn name(&self) -> &'static str {
        match self {
            EnergyPerformancePreference::Default => "default",
            EnergyPerformancePreference::Performance => "performance",
            EnergyPerformancePreference::BalancePerformance => "balance_performance",
            EnergyPerformancePreference::BalancePower => "balance_power",
            EnergyPerformancePreference::Power => "power",
        }
    }

    #[cfg(test)]
    fn dir_name(&self) -> &'static str {
        match self {
            EnergyPerformancePreference::Default => "default",
            EnergyPerformancePreference::Performance => "performance",
            EnergyPerformancePreference::BalancePerformance => "balance-performance",
            EnergyPerformancePreference::BalancePower => "balance-power",
            EnergyPerformancePreference::Power => "power",
        }
    }
}

impl FromDir for EnergyPerformancePreference {
    fn from_dir(dir: DirEntry) -> Result<Option<EnergyPerformancePreference>> {
        match dir.file_name().to_str() {
            Some("default") => Ok(Some(EnergyPerformancePreference::Default)),
            Some("performance") => Ok(Some(EnergyPerformancePreference::Performance)),
            Some("balance-performance") => {
                Ok(Some(EnergyPerformancePreference::BalancePerformance))
            }
            Some("balance-power") => Ok(Some(EnergyPerformancePreference::BalancePower)),
            Some("power") => Ok(Some(EnergyPerformancePreference::Power)),
            _ => bail!("Unknown epp {:?}!", dir.file_name()),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CpuOfflinePreference {
    SmallCore { min_active_threads: u32 },
    Smt { min_active_threads: u32 },
    Half { min_active_threads: u32 },
}

// CpuOfflinePreference::dir_name() is only used in tests.
#[cfg(test)]
impl CpuOfflinePreference {
    fn dir_name(&self) -> &'static str {
        match self {
            CpuOfflinePreference::SmallCore { .. } => "small-core",
            CpuOfflinePreference::Smt { .. } => "smt",
            CpuOfflinePreference::Half { .. } => "half",
        }
    }
}

impl FromDir for CpuOfflinePreference {
    fn from_dir(dir: DirEntry) -> Result<Option<CpuOfflinePreference>> {
        let min_active_threads_path = dir.path().join("min-active-threads");

        // The min-active-threads config is optional and it will be set to
        // DEFUALT_MIN_ACTIVE_THREADS if not specified
        let min_active_threads = if min_active_threads_path.exists() {
            read_file_to_u64(&min_active_threads_path).with_context(|| {
                format!(
                    "Error reading min-active-threads from {}",
                    min_active_threads_path.display()
                )
            })? as u32
        } else {
            DEFUALT_MIN_ACTIVE_THREADS
        };

        match dir.file_name().to_str() {
            Some("small-core") => Ok(Some(CpuOfflinePreference::SmallCore { min_active_threads })),
            Some("smt") => Ok(Some(CpuOfflinePreference::Smt { min_active_threads })),
            Some("half") => Ok(Some(CpuOfflinePreference::Half { min_active_threads })),
            _ => bail!("Unknown cpu-offline {:?}!", dir.file_name()),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct PowerPreferences {
    pub governor: Option<Governor>,
    pub epp: Option<EnergyPerformancePreference>,
    pub cpu_offline: Option<CpuOfflinePreference>,
}

#[derive(Copy, Clone)]
pub enum PowerPreferencesType {
    Default,
    WebRTC,
    Fullscreen,
    VmBoot,
    BorealisGaming,
    ArcvmGaming,
    BatterySaver,
}

impl PowerPreferencesType {
    fn dir_name(&self) -> &'static str {
        match self {
            PowerPreferencesType::Default => "default-power-preferences",
            PowerPreferencesType::WebRTC => "web-rtc-power-preferences",
            PowerPreferencesType::Fullscreen => "fullscreen-power-preferences",
            PowerPreferencesType::VmBoot => "vm-boot-power-preferences",
            PowerPreferencesType::BorealisGaming => "borealis-gaming-power-preferences",
            PowerPreferencesType::ArcvmGaming => "arcvm-gaming-power-preferences",
            PowerPreferencesType::BatterySaver => "battery-saver-power-preferences",
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum PowerSourceType {
    AC,
    DC,
}

impl PowerSourceType {
    fn dir_name(&self) -> &'static str {
        match self {
            PowerSourceType::AC => "ac",
            PowerSourceType::DC => "dc",
        }
    }
}

fn parse_ondemand_governor(path: &Path) -> Result<Governor> {
    let powersave_bias_path = path.join("powersave-bias");

    let powersave_bias = read_file_to_u64(&powersave_bias_path).with_context(|| {
        format!(
            "Error reading powersave-bias from {}",
            powersave_bias_path.display()
        )
    })? as u32;

    let sampling_rate_path = path.join("sampling-rate-ms");

    // The sampling-rate config is optional in the config
    let sampling_rate = if sampling_rate_path.exists() {
        let sampling_rate_ms = read_file_to_u64(&sampling_rate_path).with_context(|| {
            format!(
                "Error reading sampling-rate-ms from {}",
                sampling_rate_path.display()
            )
        })? as u32;

        // We treat the default value of 0 as unset. We do this because the kernel treats
        // a sampling rate of 0 as invalid.
        if sampling_rate_ms == 0 {
            None
        } else {
            // We convert from ms to uS to match what the kernel expects
            Some(sampling_rate_ms * 1000)
        }
    } else {
        None
    };

    Ok(Governor::Ondemand {
        powersave_bias,
        sampling_rate,
    })
}

// Returns Ok(None) when there is no sub directory in path.
// Returns error when there are multiple sub directories in path or when the
// sub directory name is not a supported governor.
fn parse_config_from_path<T: FromDir>(path: &Path) -> Result<Option<T>> {
    let mut dirs = path
        .read_dir()
        .with_context(|| format!("Failed to read governors from {}", path.display()))?;

    let first_dir = match dirs.next() {
        None => return Ok(None),
        Some(dir) => dir?,
    };

    if dirs.next().is_some() {
        bail!("Multiple governors detected in {}", path.display());
    }

    T::from_dir(first_dir)
}

/// Expects to find a directory tree as follows:
/// * /run/chromeos-config/v1/resource/
///   * {ac,dc}
///     * web-rtc-power-preferences/governor/
///       * ondemand/
///         * powersave-bias
///     * fullscreen-power-preferences/governor/
///       * schedutil/
///     * vm-boot-power-preferences/governor/..
///     * borealis-gaming-power-preferences/governor/..
///     * arcvm-gaming-power-preferences/governor/..
///     * battery-saver-power-preferences/governor/..
///     * default-power-preferences/governor/..
#[derive(Clone, Debug)]
pub struct ConfigProvider {
    config_path: PathBuf,
}

impl ConfigProvider {
    pub fn from_root(root: &Path) -> Self {
        Self {
            config_path: root.join(CHROMEOS_CONFIG_PATH),
        }
    }

    pub fn read_power_preferences(
        &self,
        power_source_type: PowerSourceType,
        power_preference_type: PowerPreferencesType,
    ) -> Result<Option<PowerPreferences>> {
        let path = self
            .config_path
            .join(RESOURCE_CONFIG_DIR)
            .join(power_source_type.dir_name())
            .join(power_preference_type.dir_name());

        if !path.exists() {
            return Ok(None);
        }

        let mut preferences: PowerPreferences = PowerPreferences {
            governor: None,
            epp: None,
            cpu_offline: None,
        };

        let governor_path = path.join("governor");
        if governor_path.exists() {
            preferences.governor = parse_config_from_path::<Governor>(&governor_path)?;
        }

        let epp_path = path.join("epp");
        if epp_path.exists() {
            preferences.epp = parse_config_from_path::<EnergyPerformancePreference>(&epp_path)?;
        }

        let cpu_offline_path = path.join("cpu-offline");
        if cpu_offline_path.exists() {
            preferences.cpu_offline =
                parse_config_from_path::<CpuOfflinePreference>(&cpu_offline_path)?;
        }

        Ok(Some(preferences))
    }
}

#[cfg(test)]
pub struct FakeConfig {
    root: tempfile::TempDir,
    config_path: PathBuf,
}

#[cfg(test)]
impl FakeConfig {
    pub fn new() -> Self {
        let root = tempfile::tempdir().unwrap();
        let config_path = root.path().join(CHROMEOS_CONFIG_PATH);
        Self { root, config_path }
    }

    pub fn mkdir(&mut self, path: &Path) {
        let path = self.config_path.join(path);
        fs::create_dir_all(path).unwrap();
    }

    pub fn write(&mut self, path: &Path, value: &[u8]) {
        let path = self.config_path.join(path);
        let parent = path.parent().unwrap();
        fs::create_dir_all(parent).unwrap();
        fs::write(path, value).unwrap();
    }

    pub fn write_power_preference(
        &mut self,
        power_source_type: PowerSourceType,
        power_preference_type: PowerPreferencesType,
        preference: &PowerPreferences,
    ) {
        let preference_path = self
            .config_path
            .join(RESOURCE_CONFIG_DIR)
            .join(power_source_type.dir_name())
            .join(power_preference_type.dir_name());

        fs::create_dir_all(&preference_path).unwrap();

        if let Some(governor) = &preference.governor {
            let governor_path = preference_path.join("governor").join(governor.name());
            fs::create_dir_all(&governor_path).unwrap();
            if let Governor::Ondemand {
                powersave_bias,
                sampling_rate,
            } = governor
            {
                fs::write(
                    governor_path.join("powersave-bias"),
                    powersave_bias.to_string().as_bytes(),
                )
                .unwrap();
                if let Some(sampling_rate) = sampling_rate {
                    // sampling_rate is in uS.
                    fs::write(
                        governor_path.join("sampling-rate-ms"),
                        (sampling_rate / 1000).to_string().as_bytes(),
                    )
                    .unwrap();
                }
            }
        }

        if let Some(epp) = preference.epp {
            let epp_path = preference_path.join("epp").join(epp.dir_name());
            fs::create_dir_all(epp_path).unwrap();
        }

        if let Some(cpu_offline) = preference.cpu_offline {
            let cpu_offline_path = preference_path
                .join("cpu-offline")
                .join(cpu_offline.dir_name());
            fs::create_dir_all(&cpu_offline_path).unwrap();

            let min_active_threads = match cpu_offline {
                CpuOfflinePreference::SmallCore { min_active_threads } => min_active_threads,
                CpuOfflinePreference::Smt { min_active_threads } => min_active_threads,
                CpuOfflinePreference::Half { min_active_threads } => min_active_threads,
            };
            fs::write(
                cpu_offline_path.join("min-active-threads"),
                min_active_threads.to_string().as_bytes(),
            )
            .unwrap();
        }
    }

    pub fn provider(&self) -> ConfigProvider {
        ConfigProvider::from_root(self.root.path())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_config_provider_empty_root() {
        let fake = FakeConfig::new();
        let provider = fake.provider();

        let preference = provider
            .read_power_preferences(PowerSourceType::AC, PowerPreferencesType::Default)
            .unwrap();

        assert!(preference.is_none());

        let preference = provider
            .read_power_preferences(PowerSourceType::DC, PowerPreferencesType::Default)
            .unwrap();

        assert!(preference.is_none());
    }

    #[test]
    fn test_config_provider_empty_dir() {
        let mut fake = FakeConfig::new();
        fake.mkdir(Path::new(RESOURCE_CONFIG_DIR));
        let provider = fake.provider();

        let preference = provider
            .read_power_preferences(PowerSourceType::AC, PowerPreferencesType::Default)
            .unwrap();

        assert!(preference.is_none());

        let preference = provider
            .read_power_preferences(PowerSourceType::DC, PowerPreferencesType::Default)
            .unwrap();

        assert!(preference.is_none());
    }

    #[test]
    fn test_config_provider_epp() {
        let power_source = (PowerSourceType::AC, "ac");
        let preference = (PowerPreferencesType::WebRTC, "web-rtc-power-preferences");
        let mut fake = FakeConfig::new();
        let path = Path::new(RESOURCE_CONFIG_DIR)
            .join(power_source.1)
            .join(preference.1)
            .join("epp")
            .join("balance-performance");
        fake.mkdir(&path);
        let provider = fake.provider();

        let actual = provider
            .read_power_preferences(power_source.0, preference.0)
            .unwrap();

        let expected = PowerPreferences {
            governor: None,
            epp: Some(EnergyPerformancePreference::BalancePerformance),
            cpu_offline: None,
        };

        assert_eq!(actual, Some(expected));
    }

    #[test]
    fn test_config_provider_ondemand_all_types() {
        let power_source_params = [(PowerSourceType::AC, "ac"), (PowerSourceType::DC, "dc")];

        let preference_params = [
            (PowerPreferencesType::Default, "default-power-preferences"),
            (PowerPreferencesType::WebRTC, "web-rtc-power-preferences"),
            (
                PowerPreferencesType::Fullscreen,
                "fullscreen-power-preferences",
            ),
            (PowerPreferencesType::VmBoot, "vm-boot-power-preferences"),
            (
                PowerPreferencesType::BorealisGaming,
                "borealis-gaming-power-preferences",
            ),
            (
                PowerPreferencesType::ArcvmGaming,
                "arcvm-gaming-power-preferences",
            ),
            (
                PowerPreferencesType::BatterySaver,
                "battery-saver-power-preferences",
            ),
        ];

        for (power_source, power_source_path) in power_source_params {
            for (preference, preference_path) in preference_params {
                let mut fake = FakeConfig::new();
                let ondemand_path = Path::new(RESOURCE_CONFIG_DIR)
                    .join(power_source_path)
                    .join(preference_path)
                    .join("governor")
                    .join("ondemand");
                fake.mkdir(&ondemand_path);

                let powersave_bias_path = ondemand_path.join("powersave-bias");
                fake.write(&powersave_bias_path, b"340");
                let provider = fake.provider();

                let actual = provider
                    .read_power_preferences(power_source, preference)
                    .unwrap();

                let expected = PowerPreferences {
                    governor: Some(Governor::Ondemand {
                        powersave_bias: 340,
                        sampling_rate: None,
                    }),
                    epp: None,
                    cpu_offline: None,
                };

                assert_eq!(actual, Some(expected));

                // Now try with a sampling_rate 0 (unset)
                let powersave_bias_path = ondemand_path.join("sampling-rate-ms");
                fake.write(&powersave_bias_path, b"0");
                let provider = fake.provider();

                let actual = provider
                    .read_power_preferences(power_source, preference)
                    .unwrap();

                let expected = PowerPreferences {
                    governor: Some(Governor::Ondemand {
                        powersave_bias: 340,
                        sampling_rate: None,
                    }),
                    epp: None,
                    cpu_offline: None,
                };

                assert_eq!(actual, Some(expected));

                // Now try with a sampling_rate 16
                fake.write(&powersave_bias_path, b"16");
                let provider = fake.provider();

                let actual = provider
                    .read_power_preferences(power_source, preference)
                    .unwrap();

                let expected = PowerPreferences {
                    governor: Some(Governor::Ondemand {
                        powersave_bias: 340,
                        sampling_rate: Some(16000),
                    }),
                    epp: None,
                    cpu_offline: None,
                };

                assert_eq!(actual, Some(expected));
            }
        }
    }

    #[test]
    fn test_fake_config_write_power_preference() {
        for power_source_type in [PowerSourceType::AC, PowerSourceType::DC] {
            for power_preference_type in [
                PowerPreferencesType::Default,
                PowerPreferencesType::WebRTC,
                PowerPreferencesType::Fullscreen,
                PowerPreferencesType::VmBoot,
                PowerPreferencesType::BorealisGaming,
                PowerPreferencesType::ArcvmGaming,
                PowerPreferencesType::BatterySaver,
            ] {
                for preference in [
                    PowerPreferences {
                        governor: Some(Governor::Conservative),
                        epp: Some(EnergyPerformancePreference::Default),
                        cpu_offline: Some(CpuOfflinePreference::SmallCore {
                            min_active_threads: 3,
                        }),
                    },
                    PowerPreferences {
                        governor: Some(Governor::Ondemand {
                            powersave_bias: 4,
                            sampling_rate: Some(5000),
                        }),
                        epp: Some(EnergyPerformancePreference::Performance),
                        cpu_offline: Some(CpuOfflinePreference::Smt {
                            min_active_threads: 6,
                        }),
                    },
                    PowerPreferences {
                        governor: Some(Governor::Ondemand {
                            powersave_bias: 7,
                            sampling_rate: None,
                        }),
                        epp: Some(EnergyPerformancePreference::BalancePerformance),
                        cpu_offline: Some(CpuOfflinePreference::Half {
                            min_active_threads: 8,
                        }),
                    },
                    PowerPreferences {
                        governor: Some(Governor::Performance),
                        epp: Some(EnergyPerformancePreference::BalancePower),
                        cpu_offline: None,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Powersave),
                        epp: Some(EnergyPerformancePreference::Power),
                        cpu_offline: None,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Schedutil),
                        epp: None,
                        cpu_offline: None,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Userspace),
                        epp: None,
                        cpu_offline: None,
                    },
                    PowerPreferences {
                        governor: None,
                        epp: None,
                        cpu_offline: None,
                    },
                ] {
                    let mut fake = FakeConfig::new();
                    fake.write_power_preference(
                        power_source_type,
                        power_preference_type,
                        &preference,
                    );
                    let provider = fake.provider();
                    let actual = provider
                        .read_power_preferences(power_source_type, power_preference_type)
                        .unwrap();
                    assert_eq!(actual, Some(preference));
                }
            }
        }
    }
}
