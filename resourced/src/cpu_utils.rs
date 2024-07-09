// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::fmt::Display;
use std::fs::read_to_string;
use std::path::Path;
use std::slice::Iter;
use std::str::FromStr;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use glob::glob;
use once_cell::sync::Lazy;
use regex::Regex;

use crate::common::read_from_file;

pub const SMT_CONTROL_PATH: &str = "sys/devices/system/cpu/smt/control";
pub const CPU_ONLINE_PATH: &str = "sys/devices/system/cpu/online";
const ROOT_CPUSET_CPUS_PATH: &str = "sys/fs/cgroup/cpuset/cpus";
const ISOLATED_CPUSET_PATH: &str = "sys/devices/system/cpu/isolated";

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
    // Parses a cpuset string: that is, a comma-separated list of ranges, where
    // each range is either in the form "l-u" or "c", where l, u, and c are
    // positive integers.  Example: 0-3,9,11-12.
    fn parse(cpuset_str: &str) -> Result<Self> {
        static CPUSET_RANGE_RE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r"^(\d+)-(\d+)$").expect("bad cpuset range RE"));
        let mut cores: Vec<usize> = vec![];
        // Check for a single newline, because trim() won't remove it.
        if cpuset_str.is_empty() || cpuset_str == "\n" {
            return Ok(Cpuset(cores));
        }
        let ranges = cpuset_str.trim().split(',');
        for range in ranges {
            if let Some(m) = CPUSET_RANGE_RE.captures(range) {
                // No errors expected here.
                let lower = m[1].parse::<usize>().expect("parse/RE mismatch 1");
                let upper = m[2].parse::<usize>().expect("parse/RE mismatch 2");
                if lower > upper {
                    bail!("bad range '{}' in cpuset '{}'", range, cpuset_str);
                }
                for x in lower..=upper {
                    cores.push(x);
                }
            } else {
                cores.push(
                    range
                        .parse::<usize>()
                        .with_context(|| format!("malformed cpuset: '{}'", cpuset_str))?,
                )
            }
        }
        // Sanity check.
        for i in 0..cores.len() - 1 {
            if cores[i] >= cores[i + 1] {
                bail!(
                    "cpuset '{}' has overlapping or out-of-order CPUs",
                    cpuset_str
                );
            }
        }
        Ok(Cpuset(cores))
    }

    fn difference(&self, other: &Self) -> Self {
        let mut result: Vec<usize> = vec![];
        if other.len() == 0 {
            self.clone()
        } else {
            // Quadratic but short.
            for cpu in self.iter() {
                if !other.iter().any(|other| cpu == other) {
                    result.push(*cpu);
                }
            }
            Cpuset(result)
        }
    }

    // Returns the cpuset of cores isolated by the "isolcpus" kernel command
    // line option.  See
    // https://docs.kernel.org/admin-guide/kernel-parameters.html#cpu-lists.
    // Note that the "isolcpus" feature is deprecated and may go away.
    // However, it is still more convenient than cpusets for some use cases.
    fn isolated_cores(root: &Path) -> Result<Self> {
        let path = root.join(ISOLATED_CPUSET_PATH);
        // Tolerate missing sysfs entry.
        if !path.exists() {
            Ok(Cpuset(vec![]))
        } else {
            let isolated_str = read_to_string(path).context("failed to get isolated cores")?;
            Self::parse(&isolated_str).context("failed to parse isolated cores")
        }
    }

    pub fn all_cores(root: &Path) -> Result<Self> {
        let cpuset_str = read_to_string(root.join(ROOT_CPUSET_CPUS_PATH))
            .context("Failed to get root cpuset")?;
        Ok(Self::parse(&cpuset_str)
            .context("failed to parse root cpuset")?
            .difference(&Self::isolated_cores(root).context("failed to compute all cores")?))
    }

    pub fn little_cores(root: &Path) -> Result<Self> {
        for property in [
            "cpu_capacity",
            "cpufreq/cpuinfo_max_freq",
            "acpi_cppc/highest_perf",
        ] {
            if let Some(cpuset) = get_cpus_with_min_property(root, property)? {
                return Ok(cpuset.difference(
                    &Self::isolated_cores(root).context("failed to compute little cores")?,
                ));
            }
        }

        Self::all_cores(root)
    }

    pub fn online_cpus(root: &Path) -> Result<Self> {
        Self::parse(read_to_string(root.join(CPU_ONLINE_PATH))?.trim_end_matches('\n'))
    }

    /// The number of cpu cores in this [Cpuset].
    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn iter(&self) -> Iter<usize> {
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
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let vector = &self.0;
        let length = vector.len();
        // "lower" tracks the index of the lower bound in a range.
        let mut lower = 0;
        for upper in 0..length {
            if upper + 1 < length && vector[upper + 1] == vector[upper] + 1 {
                // When there is a next value, and it is consecutive, continue
                // advancing the range upper bound.
                continue;
            }
            if lower > 0 {
                // At least one range was already output.
                write!(formatter, ",")?;
            }
            if upper > lower {
                write!(formatter, "{}-{}", vector[lower], vector[upper])?;
            } else {
                write!(formatter, "{}", vector[upper])?;
            }
            // Advance lower to the next array index.
            lower = upper + 1;
        }
        Ok(())
    }
}

/// Returns [Cpuset] containing cpus with the minimal value of the property.
/// If there is more than 2 property values, this returns the cpus with
/// two smallest values of the property.
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
            if property_path.exists() {
                Ok(Some((cpu_number, read_from_file(&property_path)?)))
            } else {
                Ok(None)
            }
        })
        .filter_map(|result: Result<Option<(usize, u64)>>| match result {
            Ok(Some(v)) => Some(Ok(v)),
            Ok(None) => None,
            Err(e) => Some(Err(e)),
        })
        .collect::<Result<Vec<(usize, u64)>>>()?;
    let mut properties: Vec<u64> = cpu_properties.iter().map(|(_, prop)| *prop).collect();
    properties.sort();
    properties.dedup();

    if properties.len() <= 1 {
        return Ok(None);
    }
    // If the map has more than 2 attribute values, we consider the cpus with
    // two smallest capacities / freqs as small cores.
    let small_core_limit = if properties.len() > 2 {
        properties[1]
    } else {
        properties[0]
    };

    let cpuset = cpu_properties
        .into_iter()
        .filter(|(_, prop)| *prop <= small_core_limit)
        .map(|(cpu, _)| cpu)
        .collect::<Cpuset>();

    Ok(Some(cpuset))
}

pub fn write_to_cpu_policy_patterns(pattern: &str, new_value: &str) -> Result<()> {
    let mut applied: bool = false;
    let entries: Vec<_> = glob(pattern)?.collect();

    if entries.is_empty() {
        applied = true;
    }

    for entry in entries {
        let policy_path = entry?;
        let mut affected_cpus_path = policy_path.to_path_buf();
        affected_cpus_path.set_file_name("affected_cpus");
        // Skip the policy update if there are no CPUs can be affected by policy.
        // Otherwise, write to the scaling governor may cause error.
        if affected_cpus_path.exists() {
            if let Ok(affected_cpus) = read_to_string(affected_cpus_path) {
                if affected_cpus.trim_end_matches('\n').is_empty() {
                    applied = true;
                    continue;
                }
            }
        }

        // Allow read fail due to CPU may be offlined.
        if let Ok(current_value) = read_to_string(&policy_path) {
            if current_value.trim_end_matches('\n') != new_value {
                std::fs::write(&policy_path, new_value).with_context(|| {
                    format!(
                        "Failed to set attribute to {}, new value: {}",
                        policy_path.display(),
                        new_value
                    )
                })?;
            }
            applied = true;
        }
    }

    // Fail if there are entries in the pattern but nothing is applied
    if !applied {
        bail!("Failed to read any of the pattern {}", pattern);
    }

    Ok(())
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
            let little_cores = Cpuset::little_cores(root)?;
            let all_cores = Cpuset::all_cores(root)?;
            if all_cores.len() - little_cores.len() >= min_active_threads as usize {
                update_cpu_online_status(root, &little_cores, false)?;
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
    use std::path::PathBuf;

    use tempfile::TempDir;

    use super::*;
    use crate::test_utils::*;

    fn setup_sysfs(root_dir: &TempDir) -> (PathBuf, PathBuf, PathBuf) {
        let root_path = root_dir.path().to_path_buf();
        let root_cpus_path = root_path.join(ROOT_CPUSET_CPUS_PATH).to_path_buf();
        let root_cpus_name = &root_cpus_path.display().to_string();
        let isolated_cpus_path = root_path.join(ISOLATED_CPUSET_PATH).to_path_buf();
        let isolated_cpus_name = &isolated_cpus_path.display().to_string();
        create_dir_all(root_cpus_path.parent().unwrap()).expect(root_cpus_name);
        create_dir_all(isolated_cpus_path.parent().unwrap()).expect(isolated_cpus_name);
        std::fs::write(&isolated_cpus_path, "").expect(isolated_cpus_name);
        (root_path, root_cpus_path, isolated_cpus_path)
    }

    #[test]
    fn test_cpuset_all_cores() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);

        // root cpuset cgroup is not found.
        assert!(Cpuset::all_cores(&root_path).is_err());

        std::fs::write(&root_cpus_path, "1,2,3,100")
            .unwrap_or_else(|_| panic!("{}", root_cpus_path.display().to_string()));
        assert_eq!(
            Cpuset::all_cores(&root_path).unwrap(),
            Cpuset(vec![1, 2, 3, 100])
        );
        std::fs::write(&root_cpus_path, "100").unwrap();
        assert_eq!(Cpuset::all_cores(&root_path).unwrap(), Cpuset(vec![100]));

        std::fs::write(&root_cpus_path, "2-10").unwrap();
        assert_eq!(
            Cpuset::all_cores(&root_path).unwrap(),
            Cpuset(vec![2, 3, 4, 5, 6, 7, 8, 9, 10])
        );
        std::fs::write(&root_cpus_path, "2-2").unwrap();
        assert_eq!(Cpuset::all_cores(&root_path).unwrap(), Cpuset(vec![2]));
        std::fs::write(&root_cpus_path, "2-2\n").unwrap();
        assert_eq!(Cpuset::all_cores(&root_path).unwrap(), Cpuset(vec![2]));
        std::fs::write(&root_cpus_path, "").unwrap();
        assert_eq!(Cpuset::all_cores(&root_path).unwrap(), Cpuset(vec![]));
        std::fs::write(&root_cpus_path, "\n").unwrap();
        assert_eq!(Cpuset::all_cores(&root_path).unwrap(), Cpuset(vec![]));
    }

    #[test]
    #[should_panic]
    fn test_cpuset_bad() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);
        std::fs::write(root_cpus_path, "a").unwrap();
        let _ = Cpuset::all_cores(&root_path).unwrap();
    }

    #[test]
    #[should_panic]
    fn test_cpuset_bad2() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);
        std::fs::write(root_cpus_path, ",").unwrap();
        let _ = Cpuset::all_cores(&root_path).unwrap();
    }

    #[test]
    #[should_panic]
    fn test_cpuset_bad3() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);
        std::fs::write(root_cpus_path, "1,9-8").unwrap();
        let _ = Cpuset::all_cores(&root_path).unwrap();
    }

    #[test]
    #[should_panic]
    fn test_cpuset_bad4() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);
        std::fs::write(root_cpus_path, "1,2,1").unwrap();
        let _ = Cpuset::all_cores(&root_path).unwrap();
    }

    fn create_cpus_property(root: &Path, property: &str, values: &[u64]) {
        for (i, v) in values.iter().enumerate() {
            let property_path = root.join(format!("sys/bus/cpu/devices/cpu{i}/{property}"));
            create_dir_all(property_path.parent().unwrap()).unwrap();

            std::fs::write(property_path, format!("{v}")).unwrap();
        }
    }

    #[test]
    fn test_cpuset_little_cores() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);

        std::fs::write(root_cpus_path, "0-3").unwrap();

        // Even if property files are not found, fallbacks to little cores.
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![0, 1, 2, 3])
        );

        create_cpus_property(&root_path, "cpu_capacity", &[10, 10, 10, 10]);
        create_cpus_property(&root_path, "cpufreq/cpuinfo_max_freq", &[20, 20, 20, 20]);
        create_cpus_property(&root_path, "acpi_cppc/highest_perf", &[30, 30, 30, 30]);

        // Fallback to all cores
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![0, 1, 2, 3])
        );

        create_cpus_property(&root_path, "acpi_cppc/highest_perf", &[10, 10, 30, 30]);
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![0, 1])
        );

        create_cpus_property(&root_path, "cpufreq/cpuinfo_max_freq", &[20, 20, 10, 10]);
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![2, 3])
        );

        create_cpus_property(&root_path, "cpu_capacity", &[1, 10, 1, 10]);
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![0, 2])
        );

        // Pick the two smallest attribute values.
        create_cpus_property(&root_path, "cpu_capacity", &[1, 2, 10, 2]);
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![0, 1, 3])
        );
        create_cpus_property(&root_path, "cpu_capacity", &[4, 2, 10, 1]);
        assert_eq!(
            Cpuset::little_cores(&root_path).unwrap(),
            Cpuset(vec![1, 3])
        );
    }

    #[test]
    fn test_cpuset_little_cores_less_properties() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, _) = setup_sysfs(&root_dir);

        std::fs::write(root_cpus_path, "0-3").unwrap();

        create_cpus_property(&root_path, "cpufreq/cpuinfo_max_freq", &[20, 10]);

        // Skips cores without the property file.
        assert_eq!(Cpuset::little_cores(&root_path).unwrap(), Cpuset(vec![1]));
    }

    #[test]
    fn test_cpuset_isolated_cores() {
        let root_dir = TempDir::new().unwrap();
        let (root_path, root_cpus_path, isolated_path) = setup_sysfs(&root_dir);
        std::fs::write(root_cpus_path, "0-15").unwrap();
        std::fs::write(isolated_path, "2-4,9").unwrap();
        let all_cores = Cpuset::all_cores(&root_path).unwrap();
        assert_eq!(all_cores.to_string(), "0-1,5-8,10-15");
    }

    #[test]
    fn test_cpuset_to_string() {
        assert_eq!(&Cpuset(vec![]).to_string(), "");
        assert_eq!(&Cpuset(vec![1, 2]).to_string(), "1-2");
        assert_eq!(&Cpuset(vec![1, 2, 3]).to_string(), "1-3");
        assert_eq!(&Cpuset(vec![1, 2, 3, 100]).to_string(), "1-3,100");
        assert_eq!(
            &Cpuset(vec![1, 2, 3, 5, 6, 7, 9, 99, 100]).to_string(),
            "1-3,5-7,9,99-100"
        );
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

    #[test]
    fn test_online_cpus() {
        let root_dir = tempfile::tempdir().unwrap();
        let root_path = root_dir.path();
        let cpu_online_path = root_path.join(CPU_ONLINE_PATH);
        create_dir_all(cpu_online_path.parent().unwrap()).unwrap();

        std::fs::write(&cpu_online_path, "0-3\n").unwrap();
        let cpus = Cpuset::online_cpus(root_path).unwrap();
        assert_eq!(cpus.0, vec![0, 1, 2, 3]);

        std::fs::write(&cpu_online_path, "0,3-5\n").unwrap();
        let cpus = Cpuset::online_cpus(root_path).unwrap();
        assert_eq!(cpus.0, vec![0, 3, 4, 5]);

        std::fs::write(&cpu_online_path, "0-1,3-5\n").unwrap();
        let cpus = Cpuset::online_cpus(root_path).unwrap();
        assert_eq!(cpus.0, vec![0, 1, 3, 4, 5]);
    }
}
