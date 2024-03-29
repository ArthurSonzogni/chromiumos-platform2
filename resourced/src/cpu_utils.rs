// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::fmt::Display;
use std::fs::read_to_string;
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::slice::Iter;
use std::str::FromStr;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use glob::glob;
use log::info;

use crate::common::read_from_file;

pub const SMT_CONTROL_PATH: &str = "sys/devices/system/cpu/smt/control";
const ROOT_CPUSET_CPUS_PATH: &str = "sys/fs/cgroup/cpuset/cpus";
const UI_USE_FLAGS_PATH: &str = "etc/ui_use_flags.txt";

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum HotplugCpuAction {
    // Set all CPUs to online.
    OnlineAll,
    // Offline small CPUs if the device has big/little clusters and the number
    // of active big cores meets the minimum active threads.
    OfflineSmallCore { min_active_threads: u32 },
    // Offline SMT cores if available and the number of physical cores meets
    // the minimum active therads.
    OfflineSMT { min_active_threads: u32 },
    // Offline half of CPUs and ensuring at least min_active_threads remain active.
    OfflineHalf { min_active_threads: u32 },
}

/// The set of cpu core numbers
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Cpuset(Vec<usize>);

impl Cpuset {
    pub fn all_cores(root: &Path) -> Result<Self> {
        let cpuset_str = read_to_string(root.join(ROOT_CPUSET_CPUS_PATH))
            .context("Failed to get root cpuset cpus")?;
        let range_parts: Vec<&str> = cpuset_str.split('-').collect();
        if range_parts.len() == 2 {
            let start: usize = range_parts[0].trim().parse()?;
            let end: usize = range_parts[1].trim().parse()?;
            if start > end {
                bail!("Invalid CPU range: {}-{}", start, end);
            }
            Ok((start..=end).collect())
        } else {
            let cores = cpuset_str
                .split(',')
                .map(|value| value.trim().parse::<usize>().context("parse core number"))
                .collect::<Result<Self>>()?;
            Ok(cores)
        }
    }

    pub fn little_cores(root: &Path) -> Result<Self> {
        if is_big_little_supported(root)? {
            for property in [
                "cpu_capacity",
                "cpufreq/cpuinfo_max_freq",
                "acpi_cppc/highest_perf",
            ] {
                if let Some(cpuset) = get_cpus_with_min_property(root, property)? {
                    return Ok(cpuset);
                }
            }
            info!("not able to determine the little cores while big/little is supported.");
        }

        Self::all_cores(root)
    }

    /// The number of cpu cores in this [Cpuset].
    pub fn len(&self) -> usize {
        self.0.len()
    }

    fn iter(&self) -> Iter<usize> {
        self.0.iter()
    }
}

impl FromIterator<usize> for Cpuset {
    fn from_iter<T>(cpus: T) -> Self
    where
        T: IntoIterator<Item = usize>,
    {
        Self(cpus.into_iter().collect())
    }
}

impl Display for Cpuset {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut iter = self.iter();
        if let Some(first_cpu) = iter.next() {
            let mut c = *first_cpu;
            let mut is_consecutive = true;
            for cpu in iter {
                c += 1;
                if c != *cpu {
                    is_consecutive = false;
                    break;
                }
            }
            if is_consecutive && *first_cpu != c {
                return write!(f, "{}-{}", first_cpu, c);
            }
        }

        let mut iter = self.iter();
        if let Some(cpu) = iter.next() {
            write!(f, "{}", cpu)?;
        }
        for cpu in iter {
            write!(f, ",{}", cpu)?;
        }
        Ok(())
    }
}

/// Returns [Cpuset] containing cpus with the minimal value of the property.
///
/// Returns [None] if:
///
/// * The property file does not exist or
/// * All cpus have the same property value.
///
/// The properties are read from /sys/bus/cpu/devices/cpu*/{property name}.
/// E.g., this function returns "0,1" for the following cpu properties.
/// | cpu # | property value |
/// |-------|----------------|
/// |   0   |       512      |
/// |   1   |       512      |
/// |   2   |      1024      |
/// |   3   |      1024      |
fn get_cpus_with_min_property(root: &Path, property: &str) -> Result<Option<Cpuset>> {
    if !root
        .join("sys/bus/cpu/devices/cpu0")
        .join(property)
        .exists()
    {
        return Ok(None);
    }

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
            let cpu_number: usize = cpu_dir
                .to_str()
                .context("Failed to convert cpu path to string")?
                .strip_prefix(&cpu_pattern_prefix)
                .context("Failed to strip prefix")?
                .parse()?;
            let property_path = Path::new(&cpu_dir).join(property);
            Ok((cpu_number, read_from_file(&property_path)?))
        })
        .collect::<Result<Vec<(usize, u64)>, anyhow::Error>>()?;
    let min_property = *cpu_properties
        .iter()
        .map(|(_, prop)| prop)
        .min()
        .context("cpu properties vector is empty")?;

    let num_all_cpus = cpu_properties.len();

    let cpuset = cpu_properties
        .into_iter()
        .filter(|(_, prop)| *prop == min_property)
        .map(|(cpu, _)| cpu)
        .collect::<Cpuset>();

    if cpuset.len() == num_all_cpus {
        return Ok(None);
    }

    Ok(Some(cpuset))
}

fn is_big_little_supported(root: &Path) -> Result<bool> {
    let reader = BufReader::new(std::fs::File::open(root.join(UI_USE_FLAGS_PATH))?);
    for line in reader.lines() {
        if line? == "big_little" {
            return Ok(true);
        }
    }
    Ok(false)
}

// Change a group of CPU online status through sysfs.
// * `cpus_fmt` -  The format string of the target CPUs in either of the format:
//   1. a list separated by comma (,). e.g. 0,1,2,3 to set CPU 0,1,2,3
//   2. a range represented by hyphen (-). e.g. 0-3 to set CPU 0,1,2,3
// * `online` - Set true to online CUPs. Set false to offline CPUs.
fn update_cpu_online_status(root: &Path, cpuset: &Cpuset, online: bool) -> Result<()> {
    let online_value = if online { "1" } else { "0" };

    for cpu in cpuset.iter() {
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

// Determines the number of physical cores on the system by analyzing the
// core_cpus_list files in sysfs. Sibling cores, sharing the same
// physical core, have identical core_cpus_list values. By identifying
// unique values, the function counts distinct physical cores.
fn get_physical_cores(root: &Path) -> Result<u32> {
    let mut core_cpus = HashSet::new();
    let core_cpus_list_pattern = root
        .join("sys/devices/system/cpu/cpu*/topology/core_cpus_list")
        .to_str()
        .context("Failed to construct thread core cpus list pattern")?
        .to_owned();
    for core_cpus_list in glob(&core_cpus_list_pattern)? {
        core_cpus.insert(read_to_string(core_cpus_list?)?);
    }

    Ok(core_cpus.len() as u32)
}

pub fn hotplug_cpus(root: &Path, action: HotplugCpuAction) -> Result<()> {
    match action {
        HotplugCpuAction::OnlineAll => {
            let all_cores = Cpuset::all_cores(root)?;
            update_cpu_smt_control(root, true)?;
            update_cpu_online_status(root, &all_cores, true)?;
        }
        HotplugCpuAction::OfflineSmallCore { min_active_threads } => {
            if is_big_little_supported(root)? {
                let little_cores = Cpuset::little_cores(root)?;
                let all_cores = Cpuset::all_cores(root)?;
                if all_cores.len() - little_cores.len() >= min_active_threads as usize {
                    update_cpu_online_status(root, &little_cores, false)?;
                }
            }
        }
        HotplugCpuAction::OfflineSMT { min_active_threads } => {
            let physical_cores = get_physical_cores(root)?;
            if physical_cores >= min_active_threads {
                update_cpu_smt_control(root, false)?;
            }
        }
        HotplugCpuAction::OfflineHalf { min_active_threads } => {
            let all_cores = Cpuset::all_cores(root)?;
            let n_all_cores = all_cores.len();
            let last_core = n_all_cores - 1;
            if n_all_cores > min_active_threads as usize {
                let first_core = std::cmp::max((last_core / 2) + 1, min_active_threads as usize);
                let half_cores = (first_core..=last_core).collect();
                update_cpu_online_status(root, &half_cores, false)?;
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use std::fs::create_dir_all;

    use tempfile::TempDir;

    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_cpuset_all_cores() {
        let root_dir = tempfile::tempdir().unwrap();
        let root_path = root_dir.path();
        let root_cpus_path = root_path.join(ROOT_CPUSET_CPUS_PATH);
        create_dir_all(root_cpus_path.parent().unwrap()).unwrap();

        // root cpuset cgroup is not found.
        assert!(Cpuset::all_cores(root_path).is_err());

        std::fs::write(&root_cpus_path, "1,2,3,100").unwrap();
        assert_eq!(
            Cpuset::all_cores(root_path).unwrap(),
            Cpuset(vec![1, 2, 3, 100])
        );

        std::fs::write(&root_cpus_path, "100").unwrap();
        assert_eq!(Cpuset::all_cores(root_path).unwrap(), Cpuset(vec![100]));

        std::fs::write(&root_cpus_path, "2-10").unwrap();
        assert_eq!(
            Cpuset::all_cores(root_path).unwrap(),
            Cpuset(vec![2, 3, 4, 5, 6, 7, 8, 9, 10])
        );

        std::fs::write(&root_cpus_path, "2-2").unwrap();
        assert_eq!(Cpuset::all_cores(root_path).unwrap(), Cpuset(vec![2]));

        std::fs::write(&root_cpus_path, "3-2").unwrap();
        assert!(Cpuset::all_cores(root_path).is_err());
        std::fs::write(&root_cpus_path, "a").unwrap();
        assert!(Cpuset::all_cores(root_path).is_err());
        std::fs::write(&root_cpus_path, "a-1").unwrap();
        assert!(Cpuset::all_cores(root_path).is_err());
        std::fs::write(&root_cpus_path, "a,100").unwrap();
        assert!(Cpuset::all_cores(root_path).is_err());
    }

    fn create_cpus_property(root: &Path, property: &str, values: &[u64]) {
        for (i, v) in values.iter().enumerate() {
            let property_path = root.join(format!("sys/bus/cpu/devices/cpu{i}/{property}"));
            create_dir_all(property_path.parent().unwrap()).unwrap();

            std::fs::write(property_path, format!("{v}")).unwrap();
        }
    }

    fn update_big_little_support(root_path: &Path, supported: bool) {
        let flag_path = root_path.join(UI_USE_FLAGS_PATH);
        create_dir_all(flag_path.parent().unwrap()).unwrap();
        if supported {
            std::fs::write(flag_path, "big_little").unwrap();
        } else {
            std::fs::write(flag_path, "").unwrap();
        }
    }

    #[test]
    fn test_cpuset_little_cores() {
        let root_dir = tempfile::tempdir().unwrap();
        let root_path = root_dir.path();
        let root_cpus_path = root_path.join(ROOT_CPUSET_CPUS_PATH);
        create_dir_all(root_cpus_path.parent().unwrap()).unwrap();
        std::fs::write(&root_cpus_path, "0-3").unwrap();
        update_big_little_support(root_path, true);

        // Even if property files are not found, fallbacks to little cores.
        assert_eq!(
            Cpuset::little_cores(root_path).unwrap(),
            Cpuset(vec![0, 1, 2, 3])
        );

        create_cpus_property(root_path, "cpu_capacity", &[10, 10, 10, 10]);
        create_cpus_property(root_path, "cpufreq/cpuinfo_max_freq", &[20, 20, 20, 20]);
        create_cpus_property(root_path, "acpi_cppc/highest_perf", &[30, 30, 30, 30]);

        // Fallback to all cores
        assert_eq!(
            Cpuset::little_cores(root_path).unwrap(),
            Cpuset(vec![0, 1, 2, 3])
        );

        create_cpus_property(root_path, "acpi_cppc/highest_perf", &[10, 10, 30, 30]);
        assert_eq!(Cpuset::little_cores(root_path).unwrap(), Cpuset(vec![0, 1]));

        create_cpus_property(root_path, "cpufreq/cpuinfo_max_freq", &[20, 20, 10, 10]);
        assert_eq!(Cpuset::little_cores(root_path).unwrap(), Cpuset(vec![2, 3]));

        create_cpus_property(root_path, "cpu_capacity", &[1, 10, 1, 10]);
        assert_eq!(Cpuset::little_cores(root_path).unwrap(), Cpuset(vec![0, 2]));

        update_big_little_support(root_path, false);
        assert_eq!(
            Cpuset::little_cores(root_path).unwrap(),
            Cpuset(vec![0, 1, 2, 3])
        );
    }

    #[test]
    fn test_cpuset_to_string() {
        assert_eq!(&Cpuset(vec![1, 2]).to_string(), "1-2");
        assert_eq!(&Cpuset(vec![1, 2, 3]).to_string(), "1-3");
        assert_eq!(&Cpuset(vec![1, 2, 3, 100]).to_string(), "1,2,3,100");
        assert_eq!(&Cpuset(vec![100]).to_string(), "100");
    }

    #[test]
    fn test_hotplug_cpus() {
        const ONLINE: &str = "1";
        const OFFLINE: &str = "0";
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
            // Test offline small cores
            Test {
                cpus: "0-3",
                big_little: true,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [1800000; 2],
                hotplug: &HotplugCpuAction::OfflineSmallCore {
                    min_active_threads: 2,
                },
                smt: "on",
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [OFFLINE; 2],
                smt_expected_state: "",
            },
            // Test offline small cores
            // No CPU offline when big cores less than min-active-theads
            Test {
                cpus: "0-3",
                big_little: true,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [1800000; 2],
                hotplug: &HotplugCpuAction::OfflineSmallCore {
                    min_active_threads: 3,
                },
                smt: "on",
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [ONLINE; 2],
                smt_expected_state: "",
            },
            // Test offline half cores
            // Offline half when min-active-theads equals half cores
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineHalf {
                    min_active_threads: 2,
                },
                smt: "on",
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [OFFLINE; 2],
                smt_expected_state: "",
            },
            // Test offline half cores
            // CPU offline starts from min-active-threads when
            // min-active-theads greater than half cores
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineHalf {
                    min_active_threads: 3,
                },
                smt: "on",
                // Expect 3 cores online
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [ONLINE, OFFLINE],
                smt_expected_state: "",
            },
            // Test offline half cores
            // No CPU offline when min-active-theads less than all cores
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineHalf {
                    min_active_threads: 5,
                },
                smt: "on",
                // Expect 3 cores online
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [ONLINE; 2],
                smt_expected_state: "",
            },
            // Test offline SMT
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineSMT {
                    min_active_threads: 2,
                },
                smt: "on",
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [OFFLINE; 2],
                smt_expected_state: "off",
            },
            // Test offline SMT
            // No CPU offline when physical cores less than min-active-theads
            Test {
                cpus: "0-3",
                big_little: false,
                cluster1_freq: [2400000; 2],
                cluster2_freq: [2400000; 2],
                hotplug: &HotplugCpuAction::OfflineSMT {
                    min_active_threads: 3,
                },
                smt: "on",
                cluster1_expected_state: [ONLINE; 2],
                cluster2_expected_state: [ONLINE; 2],
                smt_expected_state: "on",
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
            // Setup core cpus list for two physical cores and two virtual cores
            test_write_core_cpus_list(root.path(), 0, "0,2");
            test_write_core_cpus_list(root.path(), 1, "1,3");
            test_write_core_cpus_list(root.path(), 2, "0,2");
            test_write_core_cpus_list(root.path(), 3, "1,3");

            // Call function to test.
            hotplug_cpus(root.path(), *test.hotplug).unwrap();

            // Check result.
            if let HotplugCpuAction::OfflineSMT {
                min_active_threads: _,
            } = test.hotplug
            {
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
