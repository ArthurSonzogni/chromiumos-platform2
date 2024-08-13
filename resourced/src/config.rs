// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::fs::DirEntry;
use std::path::Path;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use schedqos::compute_uclamp_min;
use schedqos::CpusetCgroup;
use schedqos::RtPriority;

use crate::common::read_from_file;

const CHROMEOS_CONFIG_PATH: &str = "run/chromeos-config/v1";
const RESOURCE_CONFIG_DIR: &str = "resource";
const SCHEDQOS_CONFIG_DIR: &str = "schedqos";
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
        let min_active_threads: u32 = if min_active_threads_path.exists() {
            read_from_file(&min_active_threads_path).with_context(|| {
                format!(
                    "Error reading min-active-threads from {}",
                    min_active_threads_path.display()
                )
            })?
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
    pub cpufreq_disable_boost: bool,
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

    let powersave_bias: u32 = read_from_file(&powersave_bias_path).with_context(|| {
        format!(
            "Error reading powersave-bias from {}",
            powersave_bias_path.display()
        )
    })?;

    let sampling_rate_path = path.join("sampling-rate-ms");

    // The sampling-rate config is optional in the config
    let sampling_rate = if sampling_rate_path.exists() {
        let sampling_rate_ms: u32 = read_from_file(&sampling_rate_path).with_context(|| {
            format!(
                "Error reading sampling-rate-ms from {}",
                sampling_rate_path.display()
            )
        })?;

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

/// The schedqos configuration to use for threads of a given priority.
///
/// Each field corresponds to fields in [schedqos::ThreadStateConfig].
#[derive(Default, Debug, PartialEq)]
pub struct SchedqosThreadConfig {
    pub rt_priority: Option<RtPriority>,
    pub nice: Option<i32>,
    pub uclamp_min: Option<u32>,
    pub cpuset_cgroup: Option<CpusetCgroup>,
    pub latency_sensitive: Option<bool>,
}

impl SchedqosThreadConfig {
    fn merge_into(&self, config: &mut schedqos::ThreadStateConfig) {
        if let Some(rt_priority) = self.rt_priority {
            config.rt_priority = rt_priority
        }
        if let Some(nice) = self.nice {
            config.nice = nice
        }
        if let Some(uclamp_min) = self.uclamp_min {
            config.uclamp_min = compute_uclamp_min(uclamp_min);
        }
        if let Some(cpuset_cgroup) = self.cpuset_cgroup {
            config.cpuset_cgroup = cpuset_cgroup
        }
        if let Some(latency_sensitive) = self.latency_sensitive {
            config.latency_sensitive = latency_sensitive
        }
    }

    fn parse(path: &Path) -> Result<Self> {
        let mut config = Self::default();

        let rt_priority_path = path.join("rt-priority");
        if rt_priority_path.exists() {
            let rt_priority: i32 =
                read_from_file(&rt_priority_path).context("failed to read rt-priority")?;
            let rt_priority = if rt_priority < 0 {
                RtPriority::Disabled
            } else {
                RtPriority::Always(rt_priority as u32)
            };
            config.rt_priority = Some(rt_priority);
        }

        let nice_path = path.join("nice");
        if nice_path.exists() {
            let nice = read_from_file(&nice_path).context("failed to read nice")?;
            config.nice = Some(nice);
        }

        let uclamp_min_path = path.join("uclamp-min");
        if uclamp_min_path.exists() {
            let uclamp_min =
                read_from_file(&uclamp_min_path).context("failed to read uclamp-min")?;
            if !(0..=100).contains(&uclamp_min) {
                bail!("uclamp-min {} is out of range", uclamp_min);
            }
            config.uclamp_min = Some(uclamp_min);
        }

        let cpuset_cgroup_path = path.join("cpuset-cgroup");
        if cpuset_cgroup_path.exists() {
            let cpuset_cgroup =
                fs::read_to_string(cpuset_cgroup_path).context("failed to read cpuset-cgroup")?;
            let cpuset_cgroup = match cpuset_cgroup.as_str() {
                "all" => CpusetCgroup::All,
                "efficient" => CpusetCgroup::Efficient,
                other => bail!("invalid cpuset-cgroup: {}", other),
            };
            config.cpuset_cgroup = Some(cpuset_cgroup);
        }

        let latency_sensitive_path = path.join("latency-sensitive");
        if latency_sensitive_path.exists() {
            let latency_sensitive = fs::read_to_string(latency_sensitive_path)
                .context("failed to read latency-sensitive")?;
            let latency_sensitive = match latency_sensitive.as_str() {
                "true" => true,
                "false" => false,
                other => bail!("invalid latency-sensitive: {}", other),
            };
            config.latency_sensitive = Some(latency_sensitive);
        }

        Ok(config)
    }
}

/// Config for the schedqos feature.
#[derive(Default, Debug, PartialEq)]
pub struct SchedqosConfig {
    /// The cpu share of normal cpu cgroup.
    pub normal_cpu_share: Option<u16>,
    /// The cpu share of background cpu cgroup.
    pub background_cpu_share: Option<u16>,
    /// The thread config for URGENT_BURSTY state.
    pub thread_urgent_bursty: Option<SchedqosThreadConfig>,
    /// The thread config for URGENT state.
    pub thread_urgent: Option<SchedqosThreadConfig>,
    /// The thread config for BALANCED state.
    pub thread_balanced: Option<SchedqosThreadConfig>,
    /// The thread config for ECO state.
    pub thread_eco: Option<SchedqosThreadConfig>,
    /// The thread config for UTILITY state.
    pub thread_utility: Option<SchedqosThreadConfig>,
    /// The thread config for BACKGROUND state.
    pub thread_background: Option<SchedqosThreadConfig>,
    /// The thread config for URGENT_BURSTY_SERVER state.
    pub thread_urgent_bursty_server: Option<SchedqosThreadConfig>,
    /// The thread config for URGENT_BURSTY_CLIENT state.
    pub thread_urgent_bursty_client: Option<SchedqosThreadConfig>,
}

impl SchedqosConfig {
    pub fn merge_thread_configs_into(
        &self,
        thread_configs: &mut [schedqos::ThreadStateConfig; schedqos::NUM_THREAD_STATES],
    ) {
        let configs = [
            (
                &self.thread_urgent_bursty,
                schedqos::ThreadState::UrgentBursty,
            ),
            (&self.thread_urgent, schedqos::ThreadState::Urgent),
            (&self.thread_balanced, schedqos::ThreadState::Balanced),
            (&self.thread_eco, schedqos::ThreadState::Eco),
            (&self.thread_utility, schedqos::ThreadState::Utility),
            (&self.thread_background, schedqos::ThreadState::Background),
            (
                &self.thread_urgent_bursty_server,
                schedqos::ThreadState::UrgentBurstyServer,
            ),
            (
                &self.thread_urgent_bursty_client,
                schedqos::ThreadState::UrgentBurstyClient,
            ),
        ];

        for (config, config_type) in configs {
            if let Some(config) = config {
                config.merge_into(&mut thread_configs[config_type as usize]);
            }
        }
    }

    fn parse(path: &Path) -> Result<Self> {
        let mut config = Self::default();

        let normal_cpu_share_path = path.join("normal-cpu-share");
        if normal_cpu_share_path.exists() {
            let normal_cpu_share = read_from_file(&normal_cpu_share_path)
                .context("failed to read normal-cpu-share")?;
            config.normal_cpu_share = Some(normal_cpu_share);
        }

        let background_cpu_share_path = path.join("background-cpu-share");
        if background_cpu_share_path.exists() {
            let background_cpu_share = read_from_file(&background_cpu_share_path)
                .context("failed to read background-cpu-share")?;
            config.background_cpu_share = Some(background_cpu_share);
        }

        let configs = [
            ("thread-urgent-bursty", &mut config.thread_urgent_bursty),
            ("thread-urgent", &mut config.thread_urgent),
            ("thread-balanced", &mut config.thread_balanced),
            ("thread-eco", &mut config.thread_eco),
            ("thread-utility", &mut config.thread_utility),
            ("thread-background", &mut config.thread_background),
            (
                "thread-urgent-bursty-server",
                &mut config.thread_urgent_bursty_server,
            ),
            (
                "thread-urgent-bursty-client",
                &mut config.thread_urgent_bursty_client,
            ),
        ];
        for (dir_name, config) in configs {
            let config_path = path.join(dir_name);
            if config_path.exists() {
                *config = Some(SchedqosThreadConfig::parse(&config_path).with_context(|| {
                    format!("failed to parse schedqos thread config for {}", dir_name)
                })?);
            }
        }

        Ok(config)
    }
}

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
            cpufreq_disable_boost: false,
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

        let cpufreq_disable_boost_path = path.join("cpufreq-disable-boost");
        if cpufreq_disable_boost_path.exists() {
            let cpufreq_disable_boost = fs::read_to_string(cpufreq_disable_boost_path)
                .context("failed to read cpufreq-disable-boost")?;
            let cpufreq_disable_boost = match cpufreq_disable_boost.as_str() {
                "true" => true,
                "false" => false,
                other => bail!("invalid cpufreq-disable-boost: {}", other),
            };
            preferences.cpufreq_disable_boost = cpufreq_disable_boost;
        }

        Ok(Some(preferences))
    }

    pub fn read_sched_qos_config(&self, name: &str) -> Result<Option<SchedqosConfig>> {
        let path = self.config_path.join(SCHEDQOS_CONFIG_DIR).join(name);

        if !path.exists() {
            return Ok(None);
        }

        Ok(Some(SchedqosConfig::parse(&path)?))
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

        if preference.cpufreq_disable_boost {
            let cpufreq_boost_path = preference_path.join("cpufreq-disable-boost");
            fs::write(cpufreq_boost_path, "true").unwrap();
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
            cpufreq_disable_boost: false,
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
                    cpufreq_disable_boost: false,
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
                    cpufreq_disable_boost: false,
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
                    cpufreq_disable_boost: false,
                };

                assert_eq!(actual, Some(expected));
            }
        }
    }

    #[test]
    fn test_config_provider_empty_root_schedqos() {
        let fake = FakeConfig::new();
        let provider = fake.provider();

        let config = provider.read_sched_qos_config("default").unwrap();

        assert!(config.is_none());
    }

    #[test]
    fn test_config_provider_empty_dir_schedqos() {
        let mut fake = FakeConfig::new();
        fake.mkdir(Path::new(SCHEDQOS_CONFIG_DIR));
        let provider = fake.provider();

        let config = provider.read_sched_qos_config("default").unwrap();

        assert!(config.is_none());
    }

    #[test]
    fn test_config_provider_empty_default_schedqos() {
        let mut fake = FakeConfig::new();
        fake.mkdir(&Path::new(SCHEDQOS_CONFIG_DIR).join("default"));
        let provider = fake.provider();

        let config = provider.read_sched_qos_config("default").unwrap();

        assert_eq!(config.unwrap(), SchedqosConfig::default());
    }

    #[test]
    fn test_config_provider_schedqos() {
        let mut fake = FakeConfig::new();
        let default_config_path = Path::new(SCHEDQOS_CONFIG_DIR).join("default");
        fake.write(&default_config_path.join("normal-cpu-share"), b"1000");
        fake.write(&default_config_path.join("background-cpu-share"), b"20");
        let thread_urgent_bursty_path = default_config_path.join("thread-urgent-bursty");
        fake.write(&thread_urgent_bursty_path.join("rt-priority"), b"1");
        fake.write(&thread_urgent_bursty_path.join("nice"), b"-10");
        fake.write(&thread_urgent_bursty_path.join("uclamp-min"), b"80");
        fake.write(&thread_urgent_bursty_path.join("cpuset-cgroup"), b"all");
        fake.write(
            &thread_urgent_bursty_path.join("latency-sensitive"),
            b"true",
        );
        let thread_urgent_path = default_config_path.join("thread-urgent");
        fake.write(&thread_urgent_path.join("rt-priority"), b"10");
        fake.write(&thread_urgent_path.join("nice"), b"5");
        fake.write(&thread_urgent_path.join("uclamp-min"), b"70");
        fake.write(&thread_urgent_path.join("cpuset-cgroup"), b"efficient");
        fake.write(&thread_urgent_path.join("latency-sensitive"), b"false");
        let thread_balanced_path = default_config_path.join("thread-balanced");
        fake.write(&thread_balanced_path.join("rt-priority"), b"-1");
        fake.write(&thread_balanced_path.join("nice"), b"0");
        fake.write(&thread_balanced_path.join("uclamp-min"), b"0");
        let thread_eco_path = default_config_path.join("thread-eco");
        fake.write(&thread_eco_path.join("nice"), b"19");
        fake.write(&thread_eco_path.join("uclamp-min"), b"100");
        let thread_utility_path = default_config_path.join("thread-utility");
        fake.mkdir(&thread_utility_path);
        let thread_background_path = default_config_path.join("thread-background");
        fake.mkdir(&thread_background_path);
        let thread_urgent_bursty_server_path =
            default_config_path.join("thread-urgent-bursty-server");
        fake.mkdir(&thread_urgent_bursty_server_path);
        let thread_urgent_bursty_client_path =
            default_config_path.join("thread-urgent-bursty-client");
        fake.mkdir(&thread_urgent_bursty_client_path);

        let partialconfig_path = Path::new(SCHEDQOS_CONFIG_DIR).join("partial");
        fake.write(&partialconfig_path.join("normal-cpu-share"), b"900");
        let thread_urgent_bursty_path = partialconfig_path.join("thread-urgent-bursty");
        fake.write(&thread_urgent_bursty_path.join("rt-priority"), b"2");
        let thread_urgent_path = partialconfig_path.join("thread-urgent");
        fake.mkdir(&thread_urgent_path);

        let provider = fake.provider();

        let default_config = provider.read_sched_qos_config("default").unwrap();
        assert_eq!(
            default_config.unwrap(),
            SchedqosConfig {
                normal_cpu_share: Some(1000),
                background_cpu_share: Some(20),
                thread_urgent_bursty: Some(SchedqosThreadConfig {
                    rt_priority: Some(RtPriority::Always(1)),
                    nice: Some(-10),
                    uclamp_min: Some(80),
                    cpuset_cgroup: Some(CpusetCgroup::All),
                    latency_sensitive: Some(true),
                }),
                thread_urgent: Some(SchedqosThreadConfig {
                    rt_priority: Some(RtPriority::Always(10)),
                    nice: Some(5),
                    uclamp_min: Some(70),
                    cpuset_cgroup: Some(CpusetCgroup::Efficient),
                    latency_sensitive: Some(false),
                }),
                thread_balanced: Some(SchedqosThreadConfig {
                    rt_priority: Some(RtPriority::Disabled),
                    nice: Some(0),
                    uclamp_min: Some(0),
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_eco: Some(SchedqosThreadConfig {
                    rt_priority: None,
                    nice: Some(19),
                    uclamp_min: Some(100),
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_utility: Some(SchedqosThreadConfig {
                    rt_priority: None,
                    nice: None,
                    uclamp_min: None,
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_background: Some(SchedqosThreadConfig {
                    rt_priority: None,
                    nice: None,
                    uclamp_min: None,
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_urgent_bursty_server: Some(SchedqosThreadConfig {
                    rt_priority: None,
                    nice: None,
                    uclamp_min: None,
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_urgent_bursty_client: Some(SchedqosThreadConfig {
                    rt_priority: None,
                    nice: None,
                    uclamp_min: None,
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
            }
        );

        let partial_config = provider.read_sched_qos_config("partial").unwrap();
        assert_eq!(
            partial_config.unwrap(),
            SchedqosConfig {
                normal_cpu_share: Some(900),
                background_cpu_share: None,
                thread_urgent_bursty: Some(SchedqosThreadConfig {
                    rt_priority: Some(RtPriority::Always(2)),
                    nice: None,
                    uclamp_min: None,
                    cpuset_cgroup: None,
                    latency_sensitive: None,
                }),
                thread_urgent: Some(SchedqosThreadConfig::default()),
                thread_balanced: None,
                thread_eco: None,
                thread_utility: None,
                thread_background: None,
                thread_urgent_bursty_server: None,
                thread_urgent_bursty_client: None,
            }
        );

        let empty_config = provider.read_sched_qos_config("empty").unwrap();
        assert!(empty_config.is_none());
    }

    #[test]
    fn test_schedqos_config_merge_into() {
        let mut thread_configs = [
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Always(2),
                nice: -8,
                uclamp_min: 100,
                cpuset_cgroup: CpusetCgroup::Efficient,
                latency_sensitive: false,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Disabled,
                nice: 0,
                uclamp_min: 200,
                cpuset_cgroup: CpusetCgroup::All,
                latency_sensitive: true,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Always(3),
                nice: 2,
                uclamp_min: 300,
                cpuset_cgroup: CpusetCgroup::All,
                latency_sensitive: true,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Always(3),
                nice: 2,
                uclamp_min: 300,
                cpuset_cgroup: CpusetCgroup::All,
                latency_sensitive: true,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Disabled,
                nice: 3,
                uclamp_min: 10,
                cpuset_cgroup: CpusetCgroup::Efficient,
                latency_sensitive: false,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Disabled,
                nice: 4,
                uclamp_min: 10,
                cpuset_cgroup: CpusetCgroup::Efficient,
                latency_sensitive: false,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Disabled,
                nice: 5,
                uclamp_min: 10,
                cpuset_cgroup: CpusetCgroup::Efficient,
                latency_sensitive: false,
            },
            schedqos::ThreadStateConfig {
                rt_priority: RtPriority::Disabled,
                nice: 6,
                uclamp_min: 10,
                cpuset_cgroup: CpusetCgroup::Efficient,
                latency_sensitive: false,
            },
        ];
        SchedqosConfig {
            normal_cpu_share: None,
            background_cpu_share: None,
            thread_urgent_bursty: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Always(1)),
                nice: Some(-10),
                uclamp_min: Some(80),
                cpuset_cgroup: Some(CpusetCgroup::All),
                latency_sensitive: Some(true),
            }),
            thread_urgent: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Always(10)),
                nice: Some(5),
                uclamp_min: Some(70),
                cpuset_cgroup: Some(CpusetCgroup::Efficient),
                latency_sensitive: Some(false),
            }),
            thread_balanced: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Disabled),
                nice: Some(0),
                uclamp_min: Some(0),
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
            thread_eco: Some(SchedqosThreadConfig {
                rt_priority: None,
                nice: Some(19),
                uclamp_min: None,
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
            thread_utility: Some(SchedqosThreadConfig {
                rt_priority: None,
                nice: Some(18),
                uclamp_min: None,
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
            thread_background: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Always(3)),
                nice: None,
                uclamp_min: None,
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
            thread_urgent_bursty_server: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Always(4)),
                nice: None,
                uclamp_min: None,
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
            thread_urgent_bursty_client: Some(SchedqosThreadConfig {
                rt_priority: Some(RtPriority::Always(5)),
                nice: None,
                uclamp_min: None,
                cpuset_cgroup: None,
                latency_sensitive: None,
            }),
        }
        .merge_thread_configs_into(&mut thread_configs);

        assert_eq!(
            thread_configs,
            [
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(1),
                    nice: -10,
                    uclamp_min: 819,
                    cpuset_cgroup: CpusetCgroup::All,
                    latency_sensitive: true,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(10),
                    nice: 5,
                    uclamp_min: 717,
                    cpuset_cgroup: CpusetCgroup::Efficient,
                    latency_sensitive: false,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Disabled,
                    nice: 0,
                    uclamp_min: 0,
                    cpuset_cgroup: CpusetCgroup::All,
                    latency_sensitive: true,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(3),
                    nice: 19,
                    uclamp_min: 300,
                    cpuset_cgroup: CpusetCgroup::All,
                    latency_sensitive: true,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Disabled,
                    nice: 18,
                    uclamp_min: 10,
                    cpuset_cgroup: CpusetCgroup::Efficient,
                    latency_sensitive: false,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(3),
                    nice: 4,
                    uclamp_min: 10,
                    cpuset_cgroup: CpusetCgroup::Efficient,
                    latency_sensitive: false,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(4),
                    nice: 5,
                    uclamp_min: 10,
                    cpuset_cgroup: CpusetCgroup::Efficient,
                    latency_sensitive: false,
                },
                schedqos::ThreadStateConfig {
                    rt_priority: RtPriority::Always(5),
                    nice: 6,
                    uclamp_min: 10,
                    cpuset_cgroup: CpusetCgroup::Efficient,
                    latency_sensitive: false,
                },
            ]
        );

        let before_thread_configs = thread_configs.clone();
        SchedqosConfig::default().merge_thread_configs_into(&mut thread_configs);
        assert_eq!(thread_configs, before_thread_configs);
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
                        cpufreq_disable_boost: false,
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
                        cpufreq_disable_boost: false,
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
                        cpufreq_disable_boost: false,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Performance),
                        epp: Some(EnergyPerformancePreference::BalancePower),
                        cpu_offline: None,
                        cpufreq_disable_boost: false,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Powersave),
                        epp: Some(EnergyPerformancePreference::Power),
                        cpu_offline: None,
                        cpufreq_disable_boost: false,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Schedutil),
                        epp: None,
                        cpu_offline: None,
                        cpufreq_disable_boost: false,
                    },
                    PowerPreferences {
                        governor: Some(Governor::Userspace),
                        epp: None,
                        cpu_offline: None,
                        cpufreq_disable_boost: false,
                    },
                    PowerPreferences {
                        governor: None,
                        epp: None,
                        cpu_offline: None,
                        cpufreq_disable_boost: false,
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
