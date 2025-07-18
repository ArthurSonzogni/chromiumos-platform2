// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Write;
use std::os::unix::prelude::FileExt;
use std::path::Path;
use std::path::PathBuf;
use std::str;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use glob::glob;
use log::info;
use regex::Regex;

use crate::common::read_from_file;

/// Base path for power_limit relative to rootdir.
const DEVICE_POWER_LIMIT_PATH: &str = "sys/class/powercap/intel-rapl:0";

/// Base path for cpufreq relative to rootdir.
const DEVICE_CPUFREQ_PATH: &str = "sys/devices/system/cpu/cpufreq";

/// The threshold divsor for the minimum difference between min and max freq
const CPU_DIFF_THRESHOLD_DIVISOR: i32 = 4;

/// CPU path used for MSR register
const DEV_CPU_PATH: &str = "dev/cpu/";

/// Utility class for controlling device CPU parameters.
/// To be used by resourced-nvpd communication and game mode power steering.
/// resourced-nvpd APIs documented in [`go/resourced-grpcs`](http://go/resourced-grpcs)
#[derive(Debug, Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct DeviceCpuStatus {
    power_limit_0_current_path: PathBuf,
    power_limit_0_max: u64,
    power_limit_1_current_path: PathBuf,
    power_limit_1_max: u64,
    energy_curr_path: PathBuf,
    energy_max: u64,
    cpu_max_freq_path_pattern: String,
    cpu_min_freq_path_pattern: String,
    cpu_max_freq_default: u64,
    cpu_min_freq_default: u64,
    cpuinfo_min_freq_default: u64,
    // TODO: store static CpuInfo at object creation.  Only update current_freq at runtime.
    //cpu_info: Vec<CpuStaticInfo>
}

#[derive(Debug, Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct CpuStaticInfo {
    pub core_num: i64,
    pub base_freq_khz: i64,
    pub min_freq_khz: i64,
    pub max_freq_khz: i64,
}
// TODO(syedfaaiz) : make workflow more efficient by separating cpudev object.
pub fn double_min_freq(root: &Path) -> Result<()> {
    let cpu_dev = DeviceCpuStatus::new(root.to_path_buf())?;
    cpu_dev.set_all_min_cpu_freq(cpu_dev.get_min_freq_default()? * 2)
}
// TODO(syedfaaiz) : make workflow more efficient by separating cpudev object.
pub fn set_min_cpu_freq(root: &Path) -> Result<()> {
    let cpu_dev = DeviceCpuStatus::new(root.to_path_buf())?;
    cpu_dev.set_all_min_cpu_freq(cpu_dev.get_min_freq_default()?)
}
pub fn intel_i7_or_above(root: &Path) -> Result<bool> {
    let cpuinfo = r"model name\s+:.+Intel.+ i(\d+)-.+";
    let exp = Regex::new(cpuinfo)?;
    let path = root.join("proc/cpuinfo");
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    for line in reader.lines() {
        if let Some(result) = exp.captures(&line?) {
            return Ok(result[1].to_string().parse::<i32>()? >= 7);
        }
    }
    Ok(false)
}

#[allow(dead_code)]
impl DeviceCpuStatus {
    #[inline(always)]
    fn get_sysfs_val(&self, path_buf: &Path) -> Result<u64> {
        read_from_file(&path_buf)
    }

    /// Getter for `power_limit_0` (long-term power limit).
    ///
    /// Return the power_limit_0 value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring power limit 0.
    pub fn get_pl0_curr(&self) -> Result<u64> {
        self.get_sysfs_val(&self.power_limit_0_current_path)
    }

    /// Getter for `power_limit_0_max` (maximum long-term power limit).
    ///
    /// Return the power_limit_0_max value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring pl0 max.
    pub fn get_pl0_max(&self) -> Result<u64> {
        Ok(self.power_limit_0_max)
    }

    /// Getter for `power_limit_1` (short-term power limit).
    ///
    /// Return the power_limit_1 value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring pl1.
    pub fn get_pl1_curr(&self) -> Result<u64> {
        self.get_sysfs_val(&self.power_limit_1_current_path)
    }

    /// Getter for `power_limit_1_max` (maximum long-term power limit).
    ///
    /// Return the power_limit_1_max value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring pl1 max.
    pub fn get_pl1_max(&self) -> Result<u64> {
        Ok(self.power_limit_1_max)
    }

    /// Getter for `energy_uj` (energy counter).
    ///
    /// Return the energy_uj value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring energy consumption since last counter reset.
    pub fn get_energy_curr(&self) -> Result<u64> {
        self.get_sysfs_val(&self.energy_curr_path)
    }

    /// Getter for `max_energy_range_uj` (energy counter max limit).
    ///
    /// Return the max_energy_range_uj value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring maximum energy consumption that can be stored before overflow.
    pub fn get_energy_max(&self) -> Result<u64> {
        Ok(self.energy_max)
    }

    /// Getter for `cpuinfo_min_freq_default` (default min freq).
    ///
    /// Return the cpuinfo_min_freq_default value on supported device.
    ///
    /// # Return
    ///
    /// u64 representring minimum cpu frequency
    pub fn get_min_freq_default(&self) -> Result<u64> {
        Ok(self.cpuinfo_min_freq_default)
    }

    /// Setter for `scaling_max_freq` (per-core max clock).
    ///
    /// Sets the frequency ceiling for all cores.
    /// TODO: assumes uniform cores, expand to set p-cores and e-cores to different frequencies.
    ///
    /// # Arguments
    ///
    /// * `val` - Value to set.  If it is above max, system will set it to max. If it results in the
    ///   difference between current min and max to be less than the threshold then
    ///   the value will remain unchanged.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn set_all_max_cpu_freq(&self, val_max: u64) -> Result<()> {
        let threshold = (self.cpu_max_freq_default - self.cpu_min_freq_default)
            / (CPU_DIFF_THRESHOLD_DIVISOR as u64);
        info!("Setting All CPU max freq to {:?}", val_max);
        let cpus: Result<Vec<_>, _> = glob(&self.cpu_max_freq_path_pattern)?.collect();
        let cpus = cpus?;
        for curr_cpu in cpus {
            let cpu_min_path =
                PathBuf::from(str::replace(&curr_cpu.display().to_string(), "max", "min"));
            let val_min: u64 = read_from_file(&cpu_min_path)
                .with_context(|| format!("couldn't read {}", cpu_min_path.display()))?;
            if (val_max - val_min) > threshold {
                std::fs::write(&curr_cpu, val_max.to_string().as_bytes())
                    .with_context(|| format!("couldn't write to {}", curr_cpu.display()))?;
            } else {
                bail!("Requested frequency too close to min");
            }
        }
        Ok(())
    }

    /// Setter for `scaling_min_freq` (per-core min clock).
    ///
    /// Sets the frequency flooring for all cores.
    /// TODO: assumes uniform cores, expand to set p-cores and e-cores to different frequencies.
    ///
    /// # Arguments
    ///
    /// * `val` - Value to set.  If it is below min, system will set it to min.
    ///   If it results in the difference between current min and max to be
    ///   less than the threshold thenthe value will remain unchanged.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn set_all_min_cpu_freq(&self, val_min: u64) -> Result<()> {
        let threshold = (self.cpu_max_freq_default - self.cpu_min_freq_default)
            / (CPU_DIFF_THRESHOLD_DIVISOR as u64);
        info!("Setting All CPU min freq to {:?}", val_min);
        let cpus: Result<Vec<_>, _> = glob(&self.cpu_min_freq_path_pattern)?.collect();
        let cpus = cpus?;

        for curr_cpu in cpus {
            let cpu_max_path =
                PathBuf::from(str::replace(&curr_cpu.display().to_string(), "min", "max"));
            let val_max: u64 = read_from_file(&cpu_max_path)
                .with_context(|| format!("couldn't read {}", cpu_max_path.display()))?;
            if (val_max - val_min) > threshold {
                std::fs::write(&curr_cpu, val_min.to_string().as_bytes())
                    .with_context(|| format!("couldn't write to {}", curr_cpu.display()))?;
            } else {
                bail!("Requested frequency too close to max");
            }
        }
        Ok(())
    }

    /// Reset all cores to system default min/max frequency.
    ///
    /// Resets device to system default max frequency.  If system isn't reset after modification,
    /// min/max CPU freq will be locked/throttled until next reboot.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn reset_all_max_min_cpu_freq(&self) -> Result<()> {
        self.set_all_max_cpu_freq(self.cpu_max_freq_default)?;
        self.set_all_min_cpu_freq(self.cpu_min_freq_default)?;
        Ok(())
    }

    /// Returns the current CPU frequency oif the requested core.
    ///
    /// # Arguments
    ///
    /// * `root` - Relative path from which sysfs files are searches.
    ///   Should be `/` for non-test cases.
    ///
    /// * `core_num` - core number as defined in sysfs.
    ///
    /// # Return
    ///
    /// Result<i64> - integer denoting current frequency in KHz.
    pub fn get_core_curr_freq_khz(&self, root: PathBuf, core_num: i64) -> Result<i64> {
        let root_pathbuf = root
            .join(DEVICE_CPUFREQ_PATH)
            .join(format!("policy{core_num}/"))
            .join("scaling_cur_freq");
        let cpu_cur_freq_path = root_pathbuf.as_path();

        Ok(self.get_sysfs_val(cpu_cur_freq_path)? as i64)
    }

    /// Returns the CPU info for all available CPU cores.
    ///
    /// # Arguments
    ///
    /// * `root` - Relative path from which sysfs files are searches.
    ///   Should be `/` for non-test cases.
    ///
    /// # Return
    ///
    /// Result<Vec<CpuStaticInfo>> - CpuInfo for all cores (sorted by core_number).
    pub fn get_static_cpu_info(&self, root: PathBuf) -> Result<Vec<CpuStaticInfo>> {
        let mut res: Vec<CpuStaticInfo> = vec![];
        let mut core_num: i64;

        let cpu_policy_path = root
            .join(DEVICE_CPUFREQ_PATH)
            .join("policy*/")
            .as_path()
            .display()
            .to_string();

        if let Ok(core_paths) = glob(&cpu_policy_path) {
            let re = Regex::new(r".*cpufreq/policy(\d{1,2}).*")?;
            for core_path in core_paths.flatten() {
                let policy_path = core_path.display().to_string();

                if let Some(cap) = re.captures(&policy_path) {
                    core_num = cap
                        .get(1)
                        .map_or(0, |m| m.as_str().parse::<i64>().unwrap_or(0));
                } else {
                    bail!("Couldn't not parse core info.");
                }

                res.push(CpuStaticInfo {
                    core_num,
                    base_freq_khz: read_from_file(&core_path.join("base_frequency"))?,
                    max_freq_khz: read_from_file(&core_path.join("cpuinfo_max_freq"))?,
                    min_freq_khz: read_from_file(&core_path.join("cpuinfo_min_freq"))?,
                });
            }
        } else {
            bail!("Could not find CPU paths");
        }

        // Sort by core_number before returning.
        res.sort_by(|a, b| a.core_num.cmp(&b.core_num));
        Ok(res)
    }

    /// Create a new DeviceCpuStatus.
    ///
    /// Constructor for new DeviceCpuStatus object. Will check if all associated sysfs path exists
    /// and will return object if conditions met.  Will return an error if not all sysfs paths are
    /// found (i.e: unsupported device family, kernel version, etc.)
    ///
    /// # Arguments
    ///
    /// * `root` - root path relative to sysfs.  Will normally be '/' unless unit testing.
    ///
    /// # Return
    ///
    /// New CpuDevice object with associated functionality.
    pub fn new(root: PathBuf) -> Result<DeviceCpuStatus> {
        info!("Creating CPU device structure");
        let power_limit_0_current_path = root
            .join(DEVICE_POWER_LIMIT_PATH)
            .join("constraint_0_power_limit_uw");
        let power_limit_0_max_path = root
            .join(DEVICE_POWER_LIMIT_PATH)
            .join("constraint_0_max_power_uw");
        let power_limit_1_current_path = root
            .join(DEVICE_POWER_LIMIT_PATH)
            .join("constraint_1_power_limit_uw");
        let power_limit_1_max_path = root
            .join(DEVICE_POWER_LIMIT_PATH)
            .join("constraint_1_max_power_uw");
        let energy_curr_path = root.join(DEVICE_POWER_LIMIT_PATH).join("energy_uj");
        let energy_max_path = root
            .join(DEVICE_POWER_LIMIT_PATH)
            .join("max_energy_range_uj");

        let cpu_max_freq_path = root
            .join(DEVICE_CPUFREQ_PATH)
            .join("policy*/scaling_max_freq");
        let cpu_max_freq_path_pattern = cpu_max_freq_path.to_str().unwrap_or_default();
        let cpu_0_max_path = PathBuf::from(str::replace(cpu_max_freq_path_pattern, "*", "0"));
        // always latch baseline max, since local max may have already been modified.
        let cpu_min_freq_path = root
            .join(DEVICE_CPUFREQ_PATH)
            .join("policy*/scaling_min_freq");
        let cpu_min_freq_path_pattern = cpu_min_freq_path.to_str().unwrap_or_default();
        let cpu_0_min_path = PathBuf::from(str::replace(cpu_min_freq_path_pattern, "*", "0"));
        let cpuinfo_min_freq_path = root
            .join(DEVICE_CPUFREQ_PATH)
            .join("policy*/cpuinfo_min_freq");
        let cpuinfo_min_freq_path_pattern = cpuinfo_min_freq_path.to_str().unwrap_or_default();
        let cpuinfo_0_min_path =
            PathBuf::from(str::replace(cpuinfo_min_freq_path_pattern, "*", "0"));
        // always latch baseline min, since local min may have already been modified.
        if power_limit_0_current_path.exists()
            && power_limit_0_max_path.exists()
            && power_limit_1_current_path.exists()
            && power_limit_1_max_path.exists()
            && energy_curr_path.exists()
            && energy_max_path.exists()
            && cpu_0_max_path.exists()
            && cpu_0_min_path.exists()
            && cpuinfo_0_min_path.exists()
            && CPU_DIFF_THRESHOLD_DIVISOR > 0
        {
            info!("All sysfs file paths found");
            Ok(DeviceCpuStatus {
                power_limit_0_current_path,
                power_limit_0_max: read_from_file(&power_limit_0_max_path)?,
                power_limit_1_current_path,
                power_limit_1_max: read_from_file(&power_limit_1_max_path)?,
                energy_curr_path,
                energy_max: read_from_file(&energy_max_path)?,
                cpu_max_freq_path_pattern: cpu_max_freq_path_pattern.to_owned(),
                cpu_min_freq_path_pattern: cpu_min_freq_path_pattern.to_owned(),
                // Todo: Change to vector for ADL heterogeneous cores.
                cpu_max_freq_default: read_from_file(&cpu_0_max_path)?,
                cpu_min_freq_default: read_from_file(&cpu_0_min_path)?,
                cpuinfo_min_freq_default: read_from_file(&cpuinfo_0_min_path)?,
            })
        } else {
            info!(
                "power_limit_0_current_path.exists() {}",
                power_limit_0_current_path.exists()
            );
            info!(
                "power_limit_0_max_path.exists() {}",
                power_limit_0_max_path.exists()
            );
            info!(
                "power_limit_1_current_path.exists() {}",
                power_limit_1_current_path.exists()
            );
            info!(
                "power_limit_1_max_path.exists() {}",
                power_limit_1_max_path.exists()
            );
            info!("energy_curr_path.exists() {}", energy_curr_path.exists());
            info!("energy_max_path.exists() {}", energy_max_path.exists());
            info!(
                "cpu_max_freq_path_pattern.exists() {} (only pattern 0 checked)",
                cpu_0_max_path.exists()
            );
            info!(
                "cpu_max_freq_default_path_pattern.exists() {} (only pattern 0 checked)",
                cpu_max_freq_path.exists()
            );
            info!(
                "cpu_min_freq_path_pattern.exists() {} (only pattern 0 checked)",
                cpu_0_min_path.exists()
            );
            info!(
                "cpu_min_freq_default_path_pattern.exists() {} (only pattern 0 checked)",
                cpu_min_freq_path.exists()
            );
            info!(
                "cpuinfo_0_min_path.exists() {} (only pattern 0 checked)",
                cpuinfo_0_min_path.exists()
            );
            info!(
                "CPU_DIFF_THRESHOLD_DIVISOR == 0 {}",
                CPU_DIFF_THRESHOLD_DIVISOR == 0
            );

            bail!("Could not find all sysfs files for CPU status")
        }
    }
}

/// Write MSR registers.
///
/// Attempts to write MSR with a given address and value.
///
/// # Arguments
///
/// * `root` - root path relative to sysfs.  Will normally be '/' unless unit testing.
///
/// * `msr_value` - Value to write inside MSR register.
///
/// * `msr_addr` - Address of MSR register to write.
///
/// # Return
///
/// Result object indication success or failure.
pub fn write_msr_on_all_cpus(root: &Path, msr_value: u64, msr_addr: u64) -> Result<()> {
    let data: [u8; 8] = msr_value.to_le_bytes();

    let cpu_msr_path = root
        .join(DEV_CPU_PATH)
        .join("*/")
        .join("msr")
        .to_str()
        .context("Failed to construct {CPU_PATH}/*/msr glob pattern")?
        .to_string();

    if let Ok(msr_paths) = glob(&cpu_msr_path) {
        for msr_pathbuf in msr_paths.flatten() {
            let msr_path = Path::new(&msr_pathbuf);

            if !msr_path.exists() {
                bail!("MSR file not Found {:?}", msr_path);
            }

            let mut file = File::options().write(true).open(msr_path).context(format!(
                "Failed to open MSR file {} in rw mode",
                msr_path.display()
            ))?;

            let bytes_written = file.write_at(&data, msr_addr)?;
            if bytes_written == 8 {
                info!("Updated MSR {:02X} with value {:02X}", msr_addr, msr_value);
            } else {
                bail!(
                    "Failed to update MSR {:02X} with value {:02X}",
                    msr_addr,
                    msr_value
                );
            }
            file.flush()?;
        }
    } else {
        bail!("Could not read msr directory structure");
    }

    Ok(())
}

// Only specific Intel SoCs support setting cache allocation through MSRs
// This method checks current CPU model is one of them
pub fn intel_cache_allocation_supported(root: &Path) -> Result<bool> {
    // TODO: enable for RPL post tuning and assessment
    let llc_sharing_supported_cpu_models = [
        140, // TGL
        154, // ADL-P
             // Models to be include in the future.
             // 186, // RPL
             // 190  // ADL-N
    ];

    let cpuinfo = r"model\s+:\s+(\d+)$";
    let exp = Regex::new(cpuinfo)?;
    let path = root.join("proc/cpuinfo");
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    info!("Checking CPU Model id for cache allocation support");
    for line in reader.lines() {
        if let Some(result) = exp.captures(&line?) {
            let model_id: u16 = result[1].to_string().parse::<u16>()?;
            let supported: bool = llc_sharing_supported_cpu_models.contains(&model_id);

            if supported {
                info!("CPU Model {} supports cache allocation", model_id);
            } else {
                info!("CPU Model {} does not support cache allocation", model_id);
            }
            return Ok(supported);
        }
    }
    Ok(false)
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;
    use std::fs;

    use tempfile::tempdir;

    use super::*;
    use crate::test_utils::*;

    const MOCK_NUM_CPU: i32 = 16;

    #[test]
    fn test_sysfs_files_missing_gives_error() {
        let root = tempdir().unwrap();
        assert!(DeviceCpuStatus::new(PathBuf::from(root.path())).is_err());
    }

    #[test]
    fn test_get_pl0() {
        let root = tempdir().unwrap();
        setup_mock_cpu_dev_dirs(root.path()).unwrap();
        setup_mock_cpu_files(root.path()).unwrap();
        write_mock_cpu(root.path(), 0, 3200000, 3000000, 400000, 1000000).unwrap();
        write_mock_pl0(root.path(), 15000000).unwrap();
        let mock_cpu_dev_res = DeviceCpuStatus::new(PathBuf::from(root.path()));
        let mock_cpu_dev = mock_cpu_dev_res.unwrap();
        assert_eq!(mock_cpu_dev.get_pl0_curr().unwrap(), 15000000);
    }

    #[test]
    fn test_cpu_info_parsing() {
        let root = tempdir().unwrap();
        setup_mock_cpu_dev_dirs(root.path()).unwrap();
        setup_mock_cpu_files(root.path()).unwrap();

        let mock_cpu_dev = DeviceCpuStatus::new(PathBuf::from(root.path())).unwrap();
        let cpu_info = mock_cpu_dev
            .get_static_cpu_info(PathBuf::from(root.path()))
            .unwrap();

        // Test that cores are presorted
        for i in 0..MOCK_NUM_CPU {
            assert_eq!(cpu_info.get(i as usize).unwrap().core_num, i as i64);
        }

        // Test all cpu's were inserted with unique core_nums
        assert_eq!(
            cpu_info
                .iter()
                .map(|core| core.core_num)
                .collect::<HashSet<i64>>()
                .len() as i32,
            MOCK_NUM_CPU
        );

        // Test that correct data and paths were picked up
        let base_freqs = cpu_info
            .iter()
            .map(|core| core.base_freq_khz)
            .collect::<HashSet<i64>>();
        let min_freqs = cpu_info
            .iter()
            .map(|core| core.min_freq_khz)
            .collect::<HashSet<i64>>();
        let max_freqs = cpu_info
            .iter()
            .map(|core| core.max_freq_khz)
            .collect::<HashSet<i64>>();

        // Leave these extensible.  len will be 2 for heterogeneous cores.
        assert_eq!(base_freqs.len(), 1);
        assert_eq!(*base_freqs.iter().next().unwrap(), 2100000);

        assert_eq!(min_freqs.len(), 1);
        assert_eq!(*min_freqs.iter().next().unwrap(), 400000);

        assert_eq!(max_freqs.len(), 1);
        assert_eq!(*max_freqs.iter().next().unwrap(), 4100000);
    }

    #[test]
    fn test_cpu_read_write_reset() {
        let root = tempdir().unwrap();
        setup_mock_cpu_dev_dirs(root.path()).unwrap();
        setup_mock_cpu_files(root.path()).unwrap();
        for cpu in 0..MOCK_NUM_CPU {
            write_mock_cpu(root.path(), cpu, 3200000, 3000000, 400000, 1000000).unwrap();
        }

        let mock_cpu_dev_res = DeviceCpuStatus::new(PathBuf::from(root.path()));
        assert!(mock_cpu_dev_res.is_ok());
        let mock_cpu_dev = mock_cpu_dev_res.unwrap();
        assert_eq!(get_cpu0_freq_max(root.path()), 3000000);

        mock_cpu_dev.set_all_max_cpu_freq(2000000).unwrap();
        assert_eq!(get_cpu0_freq_max(root.path()), 2000000);

        mock_cpu_dev.set_all_min_cpu_freq(1200000).unwrap();
        assert_eq!(get_cpu0_freq_min(root.path()), 1200000);

        mock_cpu_dev.reset_all_max_min_cpu_freq().unwrap();
        assert_eq!(get_cpu0_freq_max(root.path()), 3000000);
        assert_eq!(get_cpu0_freq_min(root.path()), 1000000);

        mock_cpu_dev.set_all_max_cpu_freq(1600000).unwrap();
        assert_eq!(get_cpu0_freq_max(root.path()), 1600000);

        mock_cpu_dev.set_all_max_cpu_freq(2800000).unwrap();
        assert_eq!(get_cpu0_freq_min(root.path()), 1000000);
    }

    #[test]
    pub fn test_intel_i7_func() {
        let root = tempdir().unwrap();
        let path = root.path().join("proc");

        std::fs::create_dir_all(path.clone()).unwrap();
        std::fs::File::create(path.join("cpuinfo")).unwrap();

        std::fs::write(
            path.join("cpuinfo"),
            "model name : Intel(R) Core(TM) i7-4700HQ CPU @ 2.40GHz",
        )
        .unwrap();
        assert!(intel_i7_or_above(Path::new(root.path())).unwrap());

        std::fs::write(
            path.join("cpuinfo"),
            "model name : Intel(R) Core(TM) i5-4400HQ CPU @ 2.20GHz",
        )
        .unwrap();
        assert!(!intel_i7_or_above(Path::new(root.path())).unwrap());

        std::fs::write(
            path.join("cpuinfo"),
            "model name: AMD Ryzen Threadripper PRO 3995WX 64-Cores",
        )
        .unwrap();
        assert!(!intel_i7_or_above(Path::new(root.path())).unwrap());
    }

    #[test]
    pub fn test_is_llc_supported() {
        let root = tempdir().unwrap();
        let path = root.path().join("proc");
        fs::create_dir_all(path.clone()).unwrap();

        let file_path = path.join("cpuinfo");

        // File missing
        assert!(intel_cache_allocation_supported(root.path()).is_err());

        fs::File::create(path.join("cpuinfo")).unwrap();

        // Supported model(s)
        fs::write(&file_path, "model           : 140").unwrap();
        assert!(intel_cache_allocation_supported(root.path()).unwrap());
        fs::write(&file_path, "model           : 154").unwrap();
        assert!(intel_cache_allocation_supported(root.path()).unwrap());

        // Malformed model
        fs::write(&file_path, "no_model").unwrap();
        assert!(!intel_cache_allocation_supported(root.path()).unwrap());
        fs::write(&file_path, "model = 140").unwrap();
        assert!(!intel_cache_allocation_supported(root.path()).unwrap());

        // Unsupported model
        fs::write(&file_path, "model           : 8080").unwrap();
        assert!(!intel_cache_allocation_supported(root.path()).unwrap());
        fs::write(&file_path, "model           : 1542").unwrap();
        assert!(!intel_cache_allocation_supported(root.path()).unwrap());
        fs::write(&file_path, "model           : 140 152").unwrap();
        assert!(!intel_cache_allocation_supported(root.path()).unwrap());
    }

    pub fn setup_mock_msr_regs(root: &Path) {
        for cpu_idx in 0..MOCK_NUM_CPU {
            fs::create_dir_all(root.join(DEV_CPU_PATH).join(cpu_idx.to_string())).unwrap();

            fs::write(
                root.join(DEV_CPU_PATH)
                    .join(cpu_idx.to_string())
                    .join("msr"),
                [0u8; 100],
            )
            .unwrap();
        }
    }

    #[test]
    pub fn test_msr_write() {
        let root = tempdir().unwrap();
        let dev_cpu_path = root.path().join(DEV_CPU_PATH);

        let test_msr_addr = 0x20;
        let test_msr_val = 0x5;

        //setup
        setup_mock_msr_regs(root.path());

        for i in 0..MOCK_NUM_CPU {
            let contents = fs::read(dev_cpu_path.join(i.to_string()).join("msr")).unwrap();
            assert_eq!(0, contents.iter().map(|x| *x as i32).sum());
        }

        write_msr_on_all_cpus(root.path(), test_msr_val, test_msr_addr).unwrap();
        for i in 0..MOCK_NUM_CPU {
            let contents = fs::read(dev_cpu_path.join(i.to_string()).join("msr")).unwrap();
            assert_eq!(
                test_msr_val as i32,
                contents.iter().map(|x| *x as i32).sum()
            );
        }
    }
}
