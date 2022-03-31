// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_arch = "x86_64")]

use crate::cgroup;
use crate::common;

use anyhow::{bail, Context, Result};
use glob::glob;
use once_cell::sync::Lazy;
use std::arch::x86_64::{__cpuid, __cpuid_count};
use std::path::Path;
use std::sync::Mutex;
use sys_util::info;

const MEDIA_MIN_ECORE_NUM: u32 = 4;
static MEDIA_DYNAMIC_CGROUP_ACTIVE: Lazy<Mutex<bool>> = Lazy::new(|| Mutex::new(false));

pub enum MediaDynamicCgroupAction {
    Start,
    Stop,
}

// Check Intel hybrid platform.
pub fn is_intel_hybrid_platform() -> Result<bool> {
    // cpuid with EAX=0: Highest Function Parameter and Manufacturer ID.
    // This returns the CPU's manufacturer ID string.
    // The largest value that EAX can be set to before calling CPUID is returned in EAX.
    // https://en.wikipedia.org/wiki/CPUID.
    const CPUID_EAX_FOR_HFP_MID: u32 = 0;

    // Intel processor manufacture ID is "GenuineIntel".
    const CPUID_GENUINE_INTEL_EBX: u32 = 0x756e6547;
    const CPUID_GENUINE_INTEL_ECX: u32 = 0x6c65746e;
    const CPUID_GENUINE_INTEL_EDX: u32 = 0x49656e69;

    // cpuid with EAX=7 and ECX=0: Extended Features.
    // 15th bit in EDX tells platform has hybrid architecture or not.
    const CPUID_EAX_EXT_FEATURE: u32 = 7;
    const CPUID_ECX_EXT_FEATURE: u32 = 0;
    const CPUID_EDX_HYBRID_SHIFT: u32 = 15;

    // Read highest function parameter and manufacturer ID.
    let processor_info = unsafe { __cpuid(CPUID_EAX_FOR_HFP_MID) };

    // Check system has Intel platform i.e "GenuineIntel".
    if processor_info.ebx == CPUID_GENUINE_INTEL_EBX
        && processor_info.ecx == CPUID_GENUINE_INTEL_ECX
        && processor_info.edx == CPUID_GENUINE_INTEL_EDX
    {
        // Check system's highest function supports extended features.
        if processor_info.eax >= CPUID_EAX_EXT_FEATURE {
            // Read hybrid information.
            let hybrid_info =
                unsafe { __cpuid_count(CPUID_EAX_EXT_FEATURE, CPUID_ECX_EXT_FEATURE) };

            // Check system has Intel hybrid architecture.
            if hybrid_info.edx & (1 << CPUID_EDX_HYBRID_SHIFT) > 0 {
                return Ok(true);
            }
        }
    }
    Ok(false)
}

// Return Intel hybrid platform total number of core and ecore.
pub fn get_intel_hybrid_core_num(root: &Path) -> Result<(u32, u32)> {
    if let Some(sysfs_path) = root
        .join("sys/devices/system/cpu/cpufreq/policy*/cpuinfo_max_freq")
        .to_str()
    {
        // Total number of available cpus
        let mut total_core_num: u32 = 0;
        // Toal number of E-core
        let mut total_ecore_num: u32 = 0;

        // Frequency of P-core.
        // Intel platform policy0(cpu0) would be always P-core.
        let pcore_freq = common::read_file_to_u64(
            root.join("sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"),
        )? as u32;

        // Find total number of cpus and E-core cpus.
        for core_freq_path in glob(sysfs_path)? {
            match core_freq_path {
                Ok(path) => {
                    let core_freq = common::read_file_to_u64(path)? as u32;

                    if core_freq < pcore_freq {
                        total_ecore_num += 1;
                    }
                    total_core_num += 1;
                }
                Err(_) => bail!(
                    "Failed to read cpu frequency from path={:?}",
                    core_freq_path
                ),
            }
        }
        Ok((total_core_num, total_ecore_num))
    } else {
        bail!("Failed to construct cpuinfo_max_freq glob string");
    }
}

// Return cpulist (cpus) for Media Dynamic Cgroup feature.
fn get_media_dynamic_cgroup_cpuset_cpus(root: &Path) -> Result<String> {
    match get_intel_hybrid_core_num(root) {
        Ok((total_cpu_num, ecore_cpu_num)) => {
            // Set cpuset to first 4 E-Core CPUs.
            // e.g. Intel ADL-P-282, cpuset_head=4, cpuset_tail=7
            let cpuset_head = total_cpu_num - ecore_cpu_num;
            let cpuset_tail = cpuset_head + MEDIA_MIN_ECORE_NUM - 1;

            // Compose new cpuset for media dynamic cgroup.
            Ok((cpuset_head).to_string() + "-" + &(cpuset_tail.to_string()))
        }
        Err(_) => bail!("Failed to get hybrid cpu num"),
    }
}

// In order to use media dynamic cgroup, followings are required.
// Feature has to be turned on.
// Intel Hybird platform + number of e-core > 4
pub fn platform_feature_media_dynamic_cgroup_enabled(root: &Path) -> Result<bool> {
    let mut ecore_capable = false;

    let intel_hybrid = is_intel_hybrid_platform()?;
    if intel_hybrid {
        match get_intel_hybrid_core_num(root) {
            Ok((_total_cpu_num, ecore_cpu_num)) => {
                ecore_capable = ecore_cpu_num > MEDIA_MIN_ECORE_NUM;
            }
            Err(_) => bail!("Failed to get core numbers"),
        }
    }
    Ok(ecore_capable)
}

pub fn media_dynamic_cgroup(
    cpuset: &cgroup::CgroupCpusetManager,
    action: MediaDynamicCgroupAction,
    root: &Path,
) -> Result<()> {
    const MEDIA_MAX_SYSTEM_LOAD: f64 = (MEDIA_MIN_ECORE_NUM + 1) as f64;

    match MEDIA_DYNAMIC_CGROUP_ACTIVE.lock() {
        Ok(mut media_dynamic_cgroup_active) => {
            match action {
                MediaDynamicCgroupAction::Start => {
                    // Let's check system load first.
                    if let Ok(recent_system_load) = common::check_system_load() {
                        if recent_system_load < MEDIA_MAX_SYSTEM_LOAD {
                            if !*media_dynamic_cgroup_active {
                                info!(
                                    "system load is reasonable so start media dynamic cgroup {}",
                                    recent_system_load
                                );

                                // Write platform cpuset to media dynamic cgroup cpuset.
                                let media_cpus = get_media_dynamic_cgroup_cpuset_cpus(root)?;
                                cpuset
                                    .write_all(&media_cpus)
                                    .context("Failed to update dynamic cgropu cpus")?;

                                *media_dynamic_cgroup_active = true;
                            }
                        } else {
                            // If media dynamic cgroup is active, stop it immediately.
                            if *media_dynamic_cgroup_active {
                                info!(
                                    "system load is high {}, stop media dynamic cgroup",
                                    recent_system_load
                                );

                                cpuset.restore_all().context("Failed to restore cpuset")?;

                                *media_dynamic_cgroup_active = false;
                            } else {
                                info!(
                                    "system load is high {}, don't start media dynamic cgroup",
                                    recent_system_load
                                );
                            }
                        }
                    } else {
                        bail!("Failed to check system load for dynamic cgroup!")
                    }
                }
                MediaDynamicCgroupAction::Stop => {
                    // Dynamic cgroup is done so restore platform cpuset!
                    if *media_dynamic_cgroup_active {
                        cpuset.restore_all().context("Failed to restore cpuset")?;
                        *media_dynamic_cgroup_active = false;
                    }
                }
            }
        }
        Err(_) => bail!("Failed to lock MEDIA_DYNAMIC_CGROUP_ACTIVE"),
    }
    Ok(())
}
