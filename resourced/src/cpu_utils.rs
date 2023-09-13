// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::read_to_string;
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::str::FromStr;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use glob::glob;

use crate::common;

pub const SMT_CONTROL_PATH: &str = "sys/devices/system/cpu/smt/control";

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum HotplugCpuAction {
    // Set all CPUs to online.
    OnlineAll,
    // Offline small CPUs if the device has big/little clusters.
    OfflineSmallCore,
    // Offline SMT cores if available.
    OfflineSMT,
    // Offline half of the CPUs if the device has >= 4 CPUs.
    OfflineHalf,
}

// Returns cpus string containing cpus with the minimal value of the property.
// The properties are read from /sys/bus/cpu/devices/cpu*/{property name}.
// E.g., this function returns "0,1" for the following cpu properties.
// | cpu # | property value |
// |-------|----------------|
// |   0   |       512      |
// |   1   |       512      |
// |   2   |      1024      |
// |   3   |      1024      |
fn get_cpus_with_min_property(root: &Path, property: &str) -> Result<String> {
    let cpu_pattern = root
        .join("sys/bus/cpu/devices/cpu*")
        .to_str()
        .context("Failed to construct cpu pattern string")?
        .to_owned();
    let cpu_pattern_prefix = root
        .join("sys/bus/cpu/devices/cpu")
        .to_str()
        .context("Failed to construct cpu path prefix")?
        .to_owned();

    let cpu_properties = glob(&cpu_pattern)?
        .map(|cpu_dir| {
            let cpu_dir = cpu_dir?;
            let cpu_number: u64 = cpu_dir
                .to_str()
                .context("Failed to convert cpu path to string")?
                .strip_prefix(&cpu_pattern_prefix)
                .context("Failed to strip prefix")?
                .parse()?;
            let property_path = Path::new(&cpu_dir).join(property);
            Ok((cpu_number, common::read_file_to_u64(property_path)?))
        })
        .collect::<Result<Vec<(u64, u64)>, anyhow::Error>>()?;
    let min_property = cpu_properties
        .iter()
        .map(|(_, prop)| prop)
        .min()
        .context("cpu properties vector is empty")?;
    let cpus = cpu_properties
        .iter()
        .filter(|(_, prop)| prop == min_property)
        .map(|(cpu, _)| cpu.to_string())
        .collect::<Vec<String>>()
        .join(",");
    Ok(cpus)
}

pub fn get_little_cores(root: &Path) -> Result<String> {
    if !is_big_little_supported(root)? {
        return get_cpuset_all_cpus(root);
    }

    let cpu0_capacity = root.join("sys/bus/cpu/devices/cpu0/cpu_capacity");

    if cpu0_capacity.exists() {
        // If cpu0/cpu_capacity exists, all cpus should have the cpu_capacity file.
        get_cpus_with_min_property(root, "cpu_capacity")
    } else {
        get_cpus_with_min_property(root, "cpufreq/cpuinfo_max_freq")
    }
}

pub fn is_big_little_supported(root: &Path) -> Result<bool> {
    const UI_USE_FLAGS_PATH: &str = "etc/ui_use_flags.txt";
    let reader = BufReader::new(std::fs::File::open(root.join(UI_USE_FLAGS_PATH))?);
    for line in reader.lines() {
        if line? == "big_little" {
            return Ok(true);
        }
    }
    Ok(false)
}

pub fn get_cpuset_all_cpus(root: &Path) -> Result<String> {
    const ROOT_CPUSET_CPUS: &str = "sys/fs/cgroup/cpuset/cpus";
    let root_cpuset_cpus = root.join(ROOT_CPUSET_CPUS);
    std::fs::read_to_string(root_cpuset_cpus).context("Failed to get root cpuset cpus")
}

// Change a group of CPU online status through sysfs.
// * `cpus_fmt` -  The format string of the target CPUs in either of the format:
//   1. a list separated by comma (,). e.g. 0,1,2,3 to set CPU 0,1,2,3
//   2. a range represented by hyphen (-). e.g. 0-3 to set CPU 0,1,2,3
// * `online` - Set true to online CUPs. Set false to offline CPUs.
fn update_cpu_online_status(root: &Path, cpus_fmt: &str, online: bool) -> Result<()> {
    let online_value = if online { "1" } else { "0" };
    let range_parts: Vec<&str> = cpus_fmt.split('-').collect();
    let mut cpus = Vec::new();
    if range_parts.len() == 2 {
        if let (Ok(start), Ok(end)) = (range_parts[0].trim().parse(), range_parts[1].trim().parse())
        {
            cpus = (start..=end).collect();
        }
    } else {
        cpus = cpus_fmt
            .split(',')
            .map(|value| value.trim().parse::<i32>())
            .filter_map(Result::ok)
            .collect();
    }

    for cpu in cpus {
        let pattern = format!("sys/devices/system/cpu/cpu{}/online", cpu);
        let cpu_path = root.join(pattern);

        if cpu_path.exists() {
            std::fs::write(cpu_path, online_value.as_bytes())?;
        }
    }

    Ok(())
}
// Simultaneous Multithreading(SMT) control is sysfs control interface
// in /sys/devices/system/cpu/smt/control
#[derive(Debug, PartialEq)]
enum SmtControlStatus {
    // SMT is enabled
    On,
    // SMT is disabled
    Off,
    // SMT is force disabled. Cannot be changed.
    ForceOff,
    // SMT is not supported by the CPU
    NotSupported,
    // SMT runtime toggling is not implemented for the architecture
    NotImplemented,
}

impl FromStr for SmtControlStatus {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.trim_end() {
            "on" => Ok(SmtControlStatus::On),
            "off" => Ok(SmtControlStatus::Off),
            "forceoff" => Ok(SmtControlStatus::ForceOff),
            "notsupported" => Ok(SmtControlStatus::NotSupported),
            "notimplemented" => Ok(SmtControlStatus::NotImplemented),
            _ => bail!("Unknown Smt Control Status: '{}'", s),
        }
    }
}

// Checks the SMT sysfs /sys/devices/system/cpu/smt/control is able to be controlled.
// The "on" and "off" are the two state can be controlled.
// Returns Ok(true) when smt control is "on" or "off"
fn is_smt_contrallable(root: &Path) -> Result<bool> {
    let control_path = root.join(SMT_CONTROL_PATH);

    if !control_path.exists() {
        return Ok(false);
    }

    let control_string = read_to_string(&control_path)?;

    match SmtControlStatus::from_str(&control_string)? {
        SmtControlStatus::On | SmtControlStatus::Off => Ok(true),
        _ => Ok(false),
    }
}

// Change the SMT control state through sysfs.
// Reference: https://docs.kernel.org/admin-guide/hw-vuln/l1tf.html#smt-control
// * `enable`:
//    Set true to enable SMT, which to online all CPUs. Try to access cpu online in sysfs has
//    no restrictions.
//    Set false to disable SMT, which to offline the non-primary CPUs. Try to set non-primary
//    CPUs to online state in sysfs will be rejected.
fn update_cpu_smt_control(root: &Path, enable: bool) -> Result<()> {
    let control = root.join(SMT_CONTROL_PATH);

    if is_smt_contrallable(root)? {
        if enable {
            std::fs::write(control, "on")?;
        } else {
            std::fs::write(control, "off")?;
        }
    }

    Ok(())
}

pub fn hotplug_cpus(root: &Path, action: HotplugCpuAction) -> Result<()> {
    match action {
        HotplugCpuAction::OnlineAll => {
            let all_cores: String = get_cpuset_all_cpus(root)?;
            update_cpu_smt_control(root, true)?;
            update_cpu_online_status(root, &all_cores, true)?;
        }
        HotplugCpuAction::OfflineSmallCore => {
            if is_big_little_supported(root)? {
                let little_cores: String = get_little_cores(root)?;
                update_cpu_online_status(root, &little_cores, false)?;
            }
        }
        HotplugCpuAction::OfflineSMT => {
            update_cpu_smt_control(root, false)?;
        }
        HotplugCpuAction::OfflineHalf => {
            let num_cores: i32 = get_cpuset_all_cpus(root)?
                .split('-')
                .last()
                .context("can't get number of cores")?
                .trim()
                .parse()?;
            if num_cores >= 3 {
                update_cpu_online_status(
                    root,
                    &format!("{}-{}", (num_cores / 2) + 1, num_cores),
                    false,
                )?;
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use tempfile::TempDir;

    use super::*;
    use crate::test_utils::tests::test_check_online_cpu;
    use crate::test_utils::tests::test_check_smt_control;
    use crate::test_utils::tests::test_write_cpu_max_freq;
    use crate::test_utils::tests::test_write_cpuset_root_cpus;
    use crate::test_utils::tests::test_write_online_cpu;
    use crate::test_utils::tests::test_write_smt_control;
    use crate::test_utils::tests::test_write_ui_use_flags;

    #[test]
    fn test_hotplug_cpus() {
        struct Test<'a> {
            cpus: &'a str,
            big_little: bool,
            cluster1_freq: [u32; 2],
            cluster2_freq: [u32; 2],
            hotplug: &'a HotplugCpuAction,
            smt: &'a str,
            cluster1_expected_state: [&'a str; 2],
            cluster2_expected_state: [&'a str; 2],
            smt_expected_state: &'a str,
        }

        let tests = [
            Test {
                cpus: "0-3",
                big_little: true,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [1800000; 2],
                hotplug: &HotplugCpuAction::OfflineSmallCore,
                smt: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "",
            },
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineHalf,
                smt: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "",
            },
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineSMT,
                smt: "on",
                cluster1_expected_state: ["1"; 2],
                cluster2_expected_state: ["0"; 2],
                smt_expected_state: "off",
            },
        ];

        for test in tests {
            // Setup.
            let root = TempDir::new().unwrap();
            test_write_cpuset_root_cpus(root.path(), test.cpus);
            test_write_smt_control(root.path(), test.smt);
            if test.big_little {
                test_write_ui_use_flags(root.path(), "big_little");
            }
            for (i, freq) in test.cluster1_freq.iter().enumerate() {
                test_write_online_cpu(root.path(), i.try_into().unwrap(), "1");
                test_write_cpu_max_freq(root.path(), i.try_into().unwrap(), *freq);
            }
            for (i, freq) in test.cluster2_freq.iter().enumerate() {
                test_write_online_cpu(
                    root.path(),
                    (test.cluster1_freq.len() + i).try_into().unwrap(),
                    "1",
                );
                test_write_cpu_max_freq(
                    root.path(),
                    (test.cluster1_freq.len() + i).try_into().unwrap(),
                    *freq,
                );
            }

            // Call function to test.
            hotplug_cpus(root.path(), *test.hotplug).unwrap();

            // Check result.
            if test.hotplug == &HotplugCpuAction::OfflineSMT {
                // The mock sysfs cannot offline the SMT CPUs, here to check the smt control state
                test_check_smt_control(root.path(), test.smt_expected_state);
                continue;
            }

            for (i, state) in test.cluster1_expected_state.iter().enumerate() {
                test_check_online_cpu(root.path(), i.try_into().unwrap(), state);
            }

            for (i, state) in test.cluster2_expected_state.iter().enumerate() {
                test_check_online_cpu(
                    root.path(),
                    (test.cluster1_expected_state.len() + i).try_into().unwrap(),
                    state,
                );
            }
        }
    }

    #[test]
    fn test_parse_smt_control_status() {
        assert_eq!(
            SmtControlStatus::from_str("on\n").unwrap(),
            SmtControlStatus::On
        );
        assert_eq!(
            SmtControlStatus::from_str("off\n").unwrap(),
            SmtControlStatus::Off
        );
        assert_eq!(
            SmtControlStatus::from_str("forceoff\n").unwrap(),
            SmtControlStatus::ForceOff
        );
        assert_eq!(
            SmtControlStatus::from_str("notsupported\n").unwrap(),
            SmtControlStatus::NotSupported
        );
        assert_eq!(
            SmtControlStatus::from_str("notimplemented\n").unwrap(),
            SmtControlStatus::NotImplemented
        );

        assert!(SmtControlStatus::from_str("").is_err());
        assert!(SmtControlStatus::from_str("abc").is_err());
    }

    #[test]
    fn test_is_smt_controllable() {
        let root = TempDir::new().unwrap();

        let tests = [
            ("on", true),
            ("off", true),
            ("forceoff", false),
            ("notsupported", false),
            ("notimplemented", false),
        ];

        for test in tests {
            test_write_smt_control(root.path(), test.0);
            let result = is_smt_contrallable(root.path()).unwrap();
            assert_eq!(result, test.1);
        }
    }

    #[test]
    fn test_update_cpu_smt_control() {
        let root = TempDir::new().unwrap();

        let tests = [
            ("on", true, "on"),
            ("on", false, "off"),
            ("off", true, "on"),
            ("off", false, "off"),
            ("forceoff", true, "forceoff"),
            ("notsupported", true, "notsupported"),
            ("notimplemented", true, "notimplemented"),
        ];
        for test in tests {
            test_write_smt_control(root.path(), test.0);
            let _ = update_cpu_smt_control(root.path(), test.1);
            test_check_smt_control(root.path(), test.2);
        }
    }
}
