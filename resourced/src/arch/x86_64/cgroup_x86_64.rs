// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::sync::Mutex;

use anyhow::Context;
use anyhow::Result;
use log::info;

use super::platform::is_intel_hybrid_platform;
use crate::common::read_from_file;
use crate::cpu_utils::Cpuset;
use crate::feature;
use crate::sync::NoPoison;

const MEDIA_MIN_ECORE_NUM: u32 = 4;

static MEDIA_DYNAMIC_CGROUP_ACTIVE: Mutex<bool> = Mutex::new(false);

// List of sysfs, resourced updates during media dynamic cgroup.
const CGROUP_CPUSET_ALL: [&str; 6] = [
    "sys/fs/cgroup/cpuset/chrome/urgent/cpus",
    "sys/fs/cgroup/cpuset/chrome/non-urgent/cpus",
    "sys/fs/cgroup/cpuset/chrome/cpus",
    "sys/fs/cgroup/cpuset/resourced/all/cpus",
    "sys/fs/cgroup/cpuset/resourced/efficient/cpus",
    "sys/fs/cgroup/cpuset/user_space/media/cpus",
];

// List of sysfs, which has no constraint (i.e allowed to use all cpus) at boot.
const CGROUP_CPUSET_NO_LIMIT: [&str; 4] = [
    "sys/fs/cgroup/cpuset/chrome/urgent/cpus",
    "sys/fs/cgroup/cpuset/chrome/cpus",
    "sys/fs/cgroup/cpuset/resourced/all/cpus",
    "sys/fs/cgroup/cpuset/user_space/media/cpus",
];

// ChromeOS limits non-urgent chrome tasks to use only power efficient cores at boot.
const CGROUP_CPUSET_NONURGENT: [&str; 2] = [
    "sys/fs/cgroup/cpuset/chrome/non-urgent/cpus",
    "sys/fs/cgroup/cpuset/resourced/efficient/cpus",
];
const SCHEDULER_NONURGENT_PATH: &str = "run/chromeos-config/v1/scheduler-tune/cpuset-nonurgent";

#[derive(PartialEq, Eq)]
pub enum MediaDynamicCgroupAction {
    Start,
    Stop,
}

const FEATURE_MEDIA_DYNAMIC_CGROUP: &str = "CrOSLateBootMediaDynamicCgroup";

pub fn register_feature() {
    feature::register_feature(FEATURE_MEDIA_DYNAMIC_CGROUP, true, None)
}

fn write_cpusets(root: &Path, cpus: &str) -> Result<()> {
    for sysfs_path in CGROUP_CPUSET_ALL.iter() {
        std::fs::write(root.join(sysfs_path), cpus).with_context(|| {
            format!(
                "Error writing to path: {}, new value: {}",
                root.join(sysfs_path).display(),
                cpus
            )
        })?;
    }
    Ok(())
}

fn get_scheduler_tune_cpuset_nonurgent(root: &Path) -> Result<Option<String>> {
    let scheduler_tune_path = root.join(SCHEDULER_NONURGENT_PATH);

    if !scheduler_tune_path.exists() {
        return Ok(None);
    }

    Ok(Some(std::fs::read_to_string(scheduler_tune_path)?))
}

fn write_default_nonurgent_cpusets(root: &Path) -> Result<()> {
    let cpuset_str = match get_scheduler_tune_cpuset_nonurgent(root) {
        Ok(Some(cpuset_str)) => cpuset_str,
        Ok(None) => Cpuset::little_cores(root)?.to_string(),
        Err(e) => {
            info!("Failed to get scheduler-tune cpuset-nonurgent, {}", e);
            Cpuset::all_cores(root)?.to_string()
        }
    };

    for cpuset_path in CGROUP_CPUSET_NONURGENT.iter() {
        std::fs::write(root.join(cpuset_path), &cpuset_str)?;
    }

    Ok(())
}

// Write cpuset/*/cpus values according to the default values in ui-pre-start [1].
// [1]: https://source.corp.google.com/chromeos_public/src/platform2/login_manager/init/scripts/ui-pre-start;rcl=5505d08e00b5c3973df4eab239142d4d2f2d0e4f;l=160
fn write_default_cpusets(root: &Path) -> Result<()> {
    // non-urgent cpuset
    write_default_nonurgent_cpusets(root)?;

    // Other cpusets
    let all_cpus = Cpuset::all_cores(root)?;

    for cpus in CGROUP_CPUSET_NO_LIMIT {
        let cpus_path = root.join(cpus);
        std::fs::write(cpus_path, all_cpus.to_string())?;
    }

    Ok(())
}

// Return Intel hybrid platform total number of core and ecore.
fn get_intel_hybrid_core_num(root: &Path) -> Result<(u32, u32)> {
    // Frequency of P-core.
    // Intel platform policy0(cpu0) would be always P-core.
    let pcore_freq: u32 =
        read_from_file(&root.join("sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"))?;

    let core_freq_vec = Cpuset::online_cpus(root)?
        .iter()
        .map(|cpu| {
            read_from_file(&root.join(format!(
                "sys/devices/system/cpu/cpufreq/policy{}/cpuinfo_max_freq",
                cpu
            )))
        })
        .collect::<Result<Vec<u32>>>()?;

    // Total number of available cpus
    let total_core_num = core_freq_vec.len() as u32;
    // Toal number of E-core
    let total_ecore_num = core_freq_vec
        .iter()
        .filter(|core_freq| core_freq < &&pcore_freq)
        .count() as u32;

    Ok((total_core_num, total_ecore_num))
}

// Return cpulist (cpus) for Media Dynamic Cgroup feature.
fn get_media_dynamic_cgroup_cpuset_cpus(root: &Path) -> Result<String> {
    let (total_cpu_num, ecore_cpu_num) = get_intel_hybrid_core_num(root)?;

    // Set cpuset to first 4 E-Core CPUs.
    // e.g. Intel ADL-P-282, cpuset_head=4, cpuset_tail=7
    let cpuset_head = total_cpu_num - ecore_cpu_num;
    let cpuset_tail = cpuset_head + MEDIA_MIN_ECORE_NUM - 1;

    // Compose new cpuset for media dynamic cgroup.
    Ok((cpuset_head).to_string() + "-" + &(cpuset_tail.to_string()))
}

// In order to use media dynamic cgroup, followings are required.
// Feature has to be turned on.
// Intel Hybird platform + number of e-core > 4
fn platform_feature_media_dynamic_cgroup_enabled(root: &Path) -> Result<bool> {
    if !is_intel_hybrid_platform()? {
        return Ok(false);
    }
    let (_total_cpu_num, ecore_cpu_num) =
        get_intel_hybrid_core_num(root).context("Failed to get core numbers")?;
    Ok(ecore_cpu_num > MEDIA_MIN_ECORE_NUM)
}

// Extracts the loadavg parsing function for unittest.
fn parse_loadavg_1min<R: BufRead>(reader: R) -> Result<f64> {
    let first_line = reader.lines().next().context("No content in buffer")??;
    let error_context =
        || -> String { format!("Couldn't parse /proc/loadavg content: \"{}\"", first_line) };
    first_line
        .split_ascii_whitespace()
        .next()
        .with_context(error_context)?
        .parse()
        .with_context(error_context)
}

// Returns the load of the last 1 minute in the /proc/loadavg.
// Example /proc/loadavg content: "0.08 0.06 0.07 1/532 5515".
// See also loadavg in https://www.kernel.org/doc/html/latest/filesystems/proc.html
fn get_loadavg_1min() -> Result<f64> {
    parse_loadavg_1min(BufReader::new(std::fs::File::open("/proc/loadavg")?))
}

fn media_dynamic_cgroup_impl(action: MediaDynamicCgroupAction, root: &Path) -> Result<()> {
    let (new_active, recent_system_load) = if action == MediaDynamicCgroupAction::Start {
        const MEDIA_MAX_SYSTEM_LOAD: f64 = (MEDIA_MIN_ECORE_NUM + 1) as f64;
        let system_load = get_loadavg_1min()?;
        (system_load < MEDIA_MAX_SYSTEM_LOAD, system_load)
    } else {
        (false, f64::NAN)
    };

    let mut active = MEDIA_DYNAMIC_CGROUP_ACTIVE.do_lock();
    if *active != new_active {
        if new_active {
            info!(
                "system load is reasonable: {}, so start media dynamic cgroup",
                recent_system_load
            );

            // Write platform cpuset to media dynamic cgroup cpuset.
            let media_cpus = get_media_dynamic_cgroup_cpuset_cpus(root)?;
            write_cpusets(root, &media_cpus).context("Failed to update dynamic cgropu cpus")?;
        } else {
            if !recent_system_load.is_nan() {
                info!(
                    "system load is high: {}, stop media dynamic cgroup",
                    recent_system_load
                );
            } else {
                info!("stop media dynamic cgroup");
            }
            write_default_cpusets(root).context("Failed to restore cpuset")?;
        }
        *active = new_active;
    }
    Ok(())
}

pub fn media_dynamic_cgroup(action: MediaDynamicCgroupAction) -> Result<()> {
    // Check whether CrOS supports media dynamic cgroup for power saving.
    if feature::is_feature_enabled(FEATURE_MEDIA_DYNAMIC_CGROUP)? {
        let root = Path::new("/");
        // Check whether platform supports media dynamic cgroup.
        if platform_feature_media_dynamic_cgroup_enabled(root)? {
            media_dynamic_cgroup_impl(action, root)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::fs::create_dir_all;
    use std::fs::write;
    use std::path::PathBuf;

    use tempfile::TempDir;

    use super::*;
    use crate::cpu_utils::CPU_ONLINE_PATH;
    use crate::test_utils::*;

    #[test]
    fn test_power_platform_feature_media_dynamic_cgroup_enabled() {
        let platform_media_dynamic_cgroup =
            platform_feature_media_dynamic_cgroup_enabled(&PathBuf::from("/"));

        assert!(platform_media_dynamic_cgroup.is_ok());

        println!(
            "Does platform support media dynamic cgroup? {}",
            platform_media_dynamic_cgroup.unwrap()
        );
    }

    #[test]
    fn test_power_get_intel_hybrid_core_num() {
        let root = TempDir::new().unwrap();

        let cpu_online_path = root.path().join(CPU_ONLINE_PATH);
        create_dir_all(cpu_online_path.parent().unwrap()).unwrap();

        write(&cpu_online_path, "0-3").unwrap();
        // Create fake sysfs ../cpufreq/policy*/cpuinfo_max_freq.
        // Start with platform with 4 ISO cores.
        for cpu in 0..4 {
            // Create fake sysfs ../cpufreq/policy*/cpufino_max_freq.
            let max_freq_path = root.path().join(format!(
                "sys/devices/system/cpu/cpufreq/policy{}/cpuinfo_max_freq",
                cpu
            ));
            test_create_parent_dir(&max_freq_path);
            std::fs::write(max_freq_path, "6000").unwrap();
        }

        // Check (total_core_num, total_ecore_num).
        let core_num = get_intel_hybrid_core_num(root.path()).unwrap();
        assert_eq!(core_num, (4, 0));

        write(&cpu_online_path, "0-11").unwrap();
        // Add fake 8 e-cores sysfs.
        for cpu in 4..12 {
            // Create fake sysfs ../cpufreq/policy*/cpufino_max_freq.
            let max_freq_path = root.path().join(format!(
                "sys/devices/system/cpu/cpufreq/policy{}/cpuinfo_max_freq",
                cpu
            ));
            test_create_parent_dir(&max_freq_path);
            std::fs::write(max_freq_path, "4000").unwrap();
        }

        // Check (total_core_num, total_ecore_num).
        let core_num = get_intel_hybrid_core_num(root.path()).unwrap();
        assert_eq!(core_num, (12, 8));

        // Set CPU8, the E-core to offline
        write(&cpu_online_path, "0-7,9-11").unwrap();
        let core_num = get_intel_hybrid_core_num(root.path()).unwrap();
        assert_eq!(core_num, (11, 7));
    }

    fn test_write_cpusets(root: &Path, cpus_content: &str) {
        for cpus in CGROUP_CPUSET_ALL.iter() {
            let cpuset_cpus = root.join(cpus);
            test_create_parent_dir(&cpuset_cpus);
            std::fs::write(cpuset_cpus, cpus_content).unwrap();
        }
    }

    fn test_write_scheduler_tune_cpuset_nonurgent(root: &Path, nonurgent_cpus: &str) {
        let scheduler_tune_nonurgent_path =
            root.join("run/chromeos-config/v1/scheduler-tune/cpuset-nonurgent");
        test_create_parent_dir(&scheduler_tune_nonurgent_path);
        std::fs::write(scheduler_tune_nonurgent_path, nonurgent_cpus).unwrap();
    }

    fn test_check_file_content(path: &Path, content: &str) {
        let file_content = std::fs::read_to_string(path).unwrap();
        assert_eq!(file_content, content);
    }

    fn test_write_cpu_capacity(root: &Path, cpu_num: u32, capacity: u32) {
        let cpu_cap_path = root.join(format!("sys/bus/cpu/devices/cpu{}/cpu_capacity", cpu_num));
        test_create_parent_dir(&cpu_cap_path);
        std::fs::write(cpu_cap_path, capacity.to_string()).unwrap();
    }

    #[test]
    fn test_write_default_cpusets_with_scheduler_tune() {
        // Setup.
        let root = TempDir::new().unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-7");
        test_write_cpusets(root.path(), "0-1"); // Init cpus.
        test_write_scheduler_tune_cpuset_nonurgent(root.path(), "0-5");

        // Call function to test.
        write_default_cpusets(root.path()).unwrap();

        // Check result.
        for cpuset_path in CGROUP_CPUSET_NO_LIMIT.iter() {
            let path = root.path().join(cpuset_path);
            test_check_file_content(&path, "0-7");
        }
        test_check_file_content(&root.path().join(SCHEDULER_NONURGENT_PATH), "0-5");
    }

    #[test]
    fn test_write_default_cpusets_without_little_cores() {
        // Setup.
        let root = TempDir::new().unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-7");
        test_write_cpusets(root.path(), "0-1"); // Init cpus.

        // Call function to test.
        write_default_cpusets(root.path()).unwrap();

        // Check result.
        for cpuset_path in CGROUP_CPUSET_NO_LIMIT.iter() {
            let path = root.path().join(cpuset_path);
            test_check_file_content(&path, "0-7");
        }
        for cpuset_path in CGROUP_CPUSET_NONURGENT.iter() {
            test_check_file_content(&root.path().join(cpuset_path), "0-7");
        }
    }

    #[test]
    fn test_write_default_cpusets_with_little_cores_capacity() {
        // Setup.
        let root = TempDir::new().unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-7");
        test_write_cpusets(root.path(), "0-1"); // Init cpus.
        for i in 0..6 {
            test_write_cpu_capacity(root.path(), i, 512);
        }
        for i in 6..8 {
            test_write_cpu_capacity(root.path(), i, 1024);
        }

        // Call function to test.
        write_default_cpusets(root.path()).unwrap();

        // Check result.
        for cpuset_path in CGROUP_CPUSET_NO_LIMIT.iter() {
            let path = root.path().join(cpuset_path);
            test_check_file_content(&path, "0-7");
        }
        for cpuset_path in CGROUP_CPUSET_NONURGENT.iter() {
            // In the sysfs, the content would be converted to "0-5". But there
            // is no auto conversion in the test temp files.
            test_check_file_content(&root.path().join(cpuset_path), "0-5");
        }
    }

    #[test]
    fn test_write_default_cpusets_with_little_cores_max_freq() {
        // Setup.
        let root = TempDir::new().unwrap();
        test_write_cpuset_root_cpus(root.path(), "0-11");
        test_write_cpusets(root.path(), "0-1"); // Init cpus.
        for i in 0..8 {
            test_write_cpu_max_freq(root.path(), i, 1800000);
        }
        for i in 8..12 {
            test_write_cpu_max_freq(root.path(), i, 2400000);
        }

        // Call function to test.
        write_default_cpusets(root.path()).unwrap();

        // Check result.
        for cpuset_path in CGROUP_CPUSET_NO_LIMIT.iter() {
            let path = root.path().join(cpuset_path);
            test_check_file_content(&path, "0-11");
        }
        for cpuset_path in CGROUP_CPUSET_NONURGENT.iter() {
            test_check_file_content(&root.path().join(cpuset_path), "0-7");
        }
    }

    #[test]
    fn test_parse_loadavg_1min() {
        assert_eq!(
            parse_loadavg_1min("0.08 0.06 0.07 1/532 5515\n".to_string().as_bytes()).unwrap(),
            0.08
        );
    }
}
