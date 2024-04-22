// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod auto_epp;
mod auto_epp_config;
mod borealis;
mod cgroup_x86_64;
mod cpu_scaling;
mod globals;
mod gpu_freq_scaling;

use std::path::Path;

use anyhow::Context;
use anyhow::Result;
pub use borealis::apply_borealis_tuning;
use log::error;

use self::cgroup_x86_64::media_dynamic_cgroup;
use self::cgroup_x86_64::MediaDynamicCgroupAction;
use self::globals::read_dynamic_epp_feature;
use self::globals::set_bsm_signal_state;
use self::globals::set_media_cgroup_state;
use self::globals::set_rtc_fs_signal_state;
use self::gpu_freq_scaling::intel_device;
use crate::common::BatterySaverMode;
use crate::common::FullscreenVideo;
use crate::common::RTCAudioActive;
use crate::config::EnergyPerformancePreference;
use crate::config::PowerPreferences;
use crate::config::PowerSourceType;
use crate::cpu_utils::write_to_cpu_policy_patterns;

fn has_epp(root_path: &Path) -> Result<bool> {
    const CPU0_EPP_PATH: &str =
        "sys/devices/system/cpu/cpufreq/policy0/energy_performance_preference";
    let pattern = root_path
        .join(CPU0_EPP_PATH)
        .to_str()
        .context("Cannot convert cpu0 epp path to string")?
        .to_owned();
    Ok(Path::new(&pattern).exists())
}

fn set_epp(root_path: &Path, epp: EnergyPerformancePreference) -> Result<()> {
    const EPP_PATTERN: &str =
        "sys/devices/system/cpu/cpufreq/policy*/energy_performance_preference";
    let epp_str = match epp {
        EnergyPerformancePreference::Default => "default",
        EnergyPerformancePreference::Performance => "performance",
        EnergyPerformancePreference::BalancePerformance => "balance_performance",
        EnergyPerformancePreference::BalancePower => "balance_power",
        EnergyPerformancePreference::Power => "power",
    };
    if has_epp(root_path)? {
        let pattern = root_path
            .join(EPP_PATTERN)
            .to_str()
            .context("Cannot convert epp path to string")?
            .to_owned();
        return write_to_cpu_policy_patterns(&pattern, epp_str);
    }
    Ok(())
}

fn get_first_scaling_governor(root_path: &Path) -> Result<String> {
    const FIRST_GOVERNOR_PATTERN: &str = "sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
    let governor = std::fs::read_to_string(root_path.join(FIRST_GOVERNOR_PATTERN))?;
    Ok(governor)
}

fn set_gt_boost_freq_mhz(mode: RTCAudioActive) -> Result<()> {
    set_gt_boost_freq_mhz_impl(Path::new("/"), mode)
}

// Extract the impl function for unittest.
fn set_gt_boost_freq_mhz_impl(root: &Path, mode: RTCAudioActive) -> Result<()> {
    let mut gpu_config = intel_device::IntelGpuDeviceConfig::new(root.to_owned(), 100)?;
    gpu_config.set_rtc_audio_active(mode == RTCAudioActive::Active)
}

pub fn apply_platform_power_settings(
    root_path: &Path,
    power_source: PowerSourceType,
    rtc: RTCAudioActive,
    fullscreen: FullscreenVideo,
    bsm: BatterySaverMode,
) -> Result<()> {
    let fullscreen_video_efficiency_mode = power_source == PowerSourceType::DC
        && (rtc == RTCAudioActive::Active || fullscreen == FullscreenVideo::Active);
    if read_dynamic_epp_feature() {
        set_rtc_fs_signal_state(fullscreen_video_efficiency_mode);
        set_media_cgroup_state(fullscreen == FullscreenVideo::Active);
        set_bsm_signal_state(bsm == BatterySaverMode::Active);
    } else if fullscreen_video_efficiency_mode {
        if let Err(err) = set_epp(root_path, EnergyPerformancePreference::BalancePower) {
            error!("Failed to set energy performance preference: {:#}", err);
        }
    } else {
        // When scaling_governor is performance, energy_performance_preference can only be
        // performance.
        //
        // Reference:
        //   https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/v6.6/drivers/cpufreq/intel_pstate.c;drc=1a868273760040b746518aca7fea4f8c07366884;l=795
        match get_first_scaling_governor(root_path) {
            Ok(governor) if governor.trim() == "performance" => {}
            _ => {
                // Default EPP
                set_epp(root_path, EnergyPerformancePreference::BalancePerformance)?;
            }
        }
    }

    match fullscreen {
        FullscreenVideo::Active => media_dynamic_cgroup(MediaDynamicCgroupAction::Start)?,
        FullscreenVideo::Inactive => media_dynamic_cgroup(MediaDynamicCgroupAction::Stop)?,
    }

    if let Err(err) = set_gt_boost_freq_mhz(rtc) {
        error!("Set boost freq not supported: {:#}", err)
    }

    Ok(())
}

pub fn apply_platform_power_preferences(
    root_path: &Path,
    preferences: &PowerPreferences,
) -> Result<()> {
    if !read_dynamic_epp_feature() {
        if let Some(epp) = preferences.epp {
            set_epp(root_path, epp)?;
        }
    }
    Ok(())
}

pub fn init() {
    cgroup_x86_64::register_feature();

    auto_epp::init();
}

#[cfg(test)]
mod tests {
    use std::fs;

    use tempfile::tempdir;

    use super::*;
    use crate::common;
    use crate::config::Governor;
    use crate::config::PowerPreferencesType;
    use crate::power::DirectoryPowerPreferencesManager;
    use crate::power::PowerPreferencesManager;
    use crate::test_utils::*;

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

    fn read_epp(root: &Path) -> Result<String> {
        let epp_path = root
            .join("sys/devices/system/cpu/cpufreq/policy0/")
            .join("energy_performance_preference");

        let epp = std::fs::read_to_string(epp_path)?;

        Ok(epp)
    }

    fn write_epp(root: &Path, value: &str, affected_cpus: &str) -> Result<()> {
        let policy_path = root.join("sys/devices/system/cpu/cpufreq/policy0");
        fs::create_dir_all(&policy_path)?;

        std::fs::write(policy_path.join("energy_performance_preference"), value)?;
        std::fs::write(policy_path.join("affected_cpus"), affected_cpus)?;

        Ok(())
    }

    #[test]
    /// Tests the various EPP permutations
    fn test_power_update_power_preferences_epp() {
        let root = tempdir().unwrap();

        write_epp(root.path(), "balance_performance", AFFECTED_CPU0).unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-3");

        let tests = [
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::DC,
                },
                RTCAudioActive::Active,
                FullscreenVideo::Inactive,
                "balance_power",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::DC,
                },
                RTCAudioActive::Inactive,
                FullscreenVideo::Active,
                "balance_power",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::DC,
                },
                RTCAudioActive::Active,
                FullscreenVideo::Active,
                "balance_power",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::DC,
                },
                RTCAudioActive::Inactive,
                FullscreenVideo::Inactive,
                "balance_performance",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::AC,
                },
                RTCAudioActive::Inactive,
                FullscreenVideo::Active,
                "balance_performance",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::AC,
                },
                RTCAudioActive::Active,
                FullscreenVideo::Active,
                "balance_performance",
            ),
            (
                FakePowerSourceProvider {
                    power_source: PowerSourceType::AC,
                },
                RTCAudioActive::Active,
                FullscreenVideo::Inactive,
                "balance_performance",
            ),
        ];

        for test in tests {
            let mut fake_config = FakeConfig::new();
            fake_config.write_power_preference(
                PowerSourceType::AC,
                PowerPreferencesType::Default,
                &PowerPreferences {
                    governor: Some(Governor::Schedutil),
                    epp: None,
                    cpu_offline: None,
                    cpufreq_disable_boost: false,
                },
            );
            let config_provider = fake_config.provider();

            let manager =
                DirectoryPowerPreferencesManager::new(root.path(), config_provider, test.0);

            manager
                .update_power_preferences(
                    test.1,
                    test.2,
                    common::GameMode::Off,
                    common::VmBootMode::Inactive,
                    common::BatterySaverMode::Inactive,
                )
                .unwrap();

            let epp = read_epp(root.path()).unwrap();

            assert_eq!(epp, test.3);
        }
    }

    #[test]
    fn test_power_update_power_preferences_battery_saver_active() {
        let temp_dir = tempdir().unwrap();
        let root = temp_dir.path();

        test_write_cpuset_root_cpus(root, "0-3");

        let power_source_provider = FakePowerSourceProvider {
            power_source: PowerSourceType::AC,
        };

        let mut fake_config = FakeConfig::new();
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::ArcvmGaming,
            &PowerPreferences {
                governor: Some(Governor::Schedutil),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::BatterySaver,
            &PowerPreferences {
                governor: Some(Governor::Conservative),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        fake_config.write_power_preference(
            PowerSourceType::AC,
            PowerPreferencesType::Default,
            &PowerPreferences {
                governor: Some(Governor::Schedutil),
                epp: None,
                cpu_offline: None,
                cpufreq_disable_boost: false,
            },
        );
        let config_provider = fake_config.provider();

        let manager =
            DirectoryPowerPreferencesManager::new(root, config_provider, power_source_provider);

        let tests = [
            (
                BatterySaverMode::Active,
                "balance_performance",
                Governor::Conservative,
                AFFECTED_CPU0, // policy0 affected_cpus
                AFFECTED_CPU1, // policy1 affected_cpus
            ),
            (
                BatterySaverMode::Inactive,
                "balance_performance",
                Governor::Schedutil,
                AFFECTED_CPU0, // policy0 affected_cpus
                AFFECTED_CPU1, // policy1 affected_cpus
            ),
            (
                BatterySaverMode::Active,
                "balance_performance",
                Governor::Conservative,
                AFFECTED_CPU_NONE, // policy0 affected_cpus, which has no affected cpus
                AFFECTED_CPU1,     // policy1 affected_cpus
            ),
        ];

        // Test device without EPP path
        for test in tests {
            let orig_governor = Governor::Performance;
            let policy0 = PolicyConfigs {
                policy_path: TEST_CPUFREQ_POLICIES[0],
                governor: &orig_governor,
                affected_cpus: test.3,
            };
            let policy1 = PolicyConfigs {
                policy_path: TEST_CPUFREQ_POLICIES[1],
                governor: &orig_governor,
                affected_cpus: test.4,
            };
            let policies = vec![policy0, policy1];
            write_per_policy_scaling_governor(root, policies);
            manager
                .update_power_preferences(
                    common::RTCAudioActive::Inactive,
                    common::FullscreenVideo::Inactive,
                    common::GameMode::Arc,
                    common::VmBootMode::Inactive,
                    test.0,
                )
                .unwrap();

            let mut expected_governors = vec![test.2, test.2];
            if test.3.is_empty() {
                expected_governors[0] = orig_governor;
            }
            check_per_policy_scaling_governor(root, expected_governors);
        }

        // Test device with EPP path
        let orig_epp = "balance_performance";
        for test in tests {
            write_epp(root, orig_epp, test.3).unwrap();
            manager
                .update_power_preferences(
                    common::RTCAudioActive::Inactive,
                    common::FullscreenVideo::Inactive,
                    common::GameMode::Arc,
                    common::VmBootMode::Inactive,
                    test.0,
                )
                .unwrap();

            let epp = read_epp(root).unwrap();
            let mut expected_epp = test.1;
            if test.3.is_empty() {
                expected_epp = orig_epp;
            }
            assert_eq!(epp, expected_epp);
        }
    }
}
