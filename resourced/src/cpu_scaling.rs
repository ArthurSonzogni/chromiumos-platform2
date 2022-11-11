// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::{bail, Result};
use glob::glob;
use libchromeos::sys::info;
use std::path::{Path, PathBuf};
use std::str;

use crate::common;

/// Base path for power_limit relative to rootdir.
const DEVICE_POWER_LIMIT_PATH: &str = "sys/class/powercap/intel-rapl:0";

/// Base path for cpufreq relative to rootdir.
const DEVICE_CPUFREQ_PATH: &str = "sys/devices/system/cpu/cpufreq";

/// The threshold divsor for the minimum difference between min and max freq
const CPU_DIFF_THRESHOLD_DIVISOR: i32 = 5;

/// Utility class for controlling device CPU parameters.
/// To be used by resourced-nvpd communication and game mode power steering.
/// resourced-nvpd APIs documented in [`go/resourced-grpcs`](http://go/resourced-grpcs)
#[derive(Debug, Clone)]
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
}

#[allow(dead_code)]
impl DeviceCpuStatus {
    #[inline(always)]
    fn get_sysfs_val(&self, path_buf: &Path) -> Result<u64> {
        common::read_file_to_u64(path_buf)
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

    /// Setter for `scaling_max_freq` (per-core max clock).
    ///
    /// Sets the frequency ceiling for all cores.
    /// TODO: assumes uniform cores, expand to set p-cores and e-cores to different frequencies.
    ///
    /// # Arguments
    ///
    /// * `val` - Value to set.  If it is above max, system will set it to max. If it results in the
    /// difference between current min and max to be less than the threshold then
    /// the value will remain unchanged.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn set_all_max_cpu_freq(&self, val_max: u64) -> Result<()> {
        let threshold = (self.cpu_max_freq_default - self.cpu_min_freq_default)
            / (CPU_DIFF_THRESHOLD_DIVISOR as u64);
        info!("Setting All CPU max freq");
        let cpus: Result<Vec<_>, _> = glob(&self.cpu_max_freq_path_pattern)?.collect();
        let cpus = cpus?;
        for curr_cpu in cpus {
            let cpu_min_path =
                PathBuf::from(str::replace(&curr_cpu.display().to_string(), "max", "min"));
            let val_min = common::read_file_to_u64(cpu_min_path)?;
            if (val_max - val_min) > threshold {
                std::fs::write(curr_cpu, val_max.to_string().as_bytes())?;
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
    /// If it results in the difference between current min and max to be
    /// less than the threshold thenthe value will remain unchanged.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn set_all_min_cpu_freq(&self, val_min: u64) -> Result<()> {
        let threshold = (self.cpu_max_freq_default - self.cpu_min_freq_default)
            / (CPU_DIFF_THRESHOLD_DIVISOR as u64);
        info!("Setting All CPU min freq");
        let cpus: Result<Vec<_>, _> = glob(&self.cpu_min_freq_path_pattern)?.collect();
        let cpus = cpus?;

        for curr_cpu in cpus {
            let cpu_max_path =
                PathBuf::from(str::replace(&curr_cpu.display().to_string(), "min", "max"));
            let val_max = common::read_file_to_u64(cpu_max_path)?;
            if (val_max - val_min) > threshold {
                std::fs::write(curr_cpu, val_min.to_string().as_bytes())?;
            } else {
                bail!("Requested frequency too close to max");
            }
        }
        Ok(())
    }

    /// Reset all cores to system default max frequency.
    ///
    /// Resets device to system default max frequency.  If system isn't reset after modification,
    /// max CPU freq will be locked/throttled until next reboot.
    ///
    /// # Return
    ///
    /// Result<()>
    pub fn reset_all_max_min_cpu_freq(&self) -> Result<()> {
        self.set_all_max_cpu_freq(self.cpu_max_freq_default)?;
        self.set_all_min_cpu_freq(self.cpu_min_freq_default)?;
        Ok(())
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
        // always latch baseline min, since local min may have already been modified.
        if power_limit_0_current_path.exists()
            && power_limit_0_max_path.exists()
            && power_limit_1_current_path.exists()
            && power_limit_1_max_path.exists()
            && energy_curr_path.exists()
            && energy_max_path.exists()
            && cpu_0_max_path.exists()
            && cpu_0_min_path.exists()
            && CPU_DIFF_THRESHOLD_DIVISOR > 0
        {
            info!("All sysfs file paths found");
            Ok(DeviceCpuStatus {
                power_limit_0_current_path,
                power_limit_0_max: common::read_file_to_u64(power_limit_0_max_path)?,
                power_limit_1_current_path,
                power_limit_1_max: common::read_file_to_u64(power_limit_1_max_path)?,
                energy_curr_path,
                energy_max: common::read_file_to_u64(energy_max_path)?,
                cpu_max_freq_path_pattern: cpu_max_freq_path_pattern.to_owned(),
                cpu_min_freq_path_pattern: cpu_min_freq_path_pattern.to_owned(),
                // Todo: Change to vector for ADL heterogeneous cores.
                cpu_max_freq_default: common::read_file_to_u64(cpu_0_max_path)?,
                cpu_min_freq_default: common::read_file_to_u64(cpu_0_min_path)?,
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
                "CPU_DIFF_THRESHOLD_DIVISOR == 0 {}",
                CPU_DIFF_THRESHOLD_DIVISOR == 0
            );

            bail!("Could not find all sysfs files for CPU status")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::Path;
    use tempfile::tempdir;

    const MOCK_NUM_CPU: i32 = 8;

    fn write_mock_pl0(root: &Path, value: u64) -> Result<()> {
        std::fs::write(
            root.join(super::DEVICE_POWER_LIMIT_PATH)
                .join("constraint_0_power_limit_uw"),
            value.to_string(),
        )?;

        Ok(())
    }

    fn write_mock_cpu(
        root: &Path,
        cpu_num: i32,
        baseline_max: u64,
        curr_max: u64,
        baseline_min: u64,
        curr_min: u64,
    ) -> Result<()> {
        let policy_path = root
            .join(super::DEVICE_CPUFREQ_PATH)
            .join(format!("policy{cpu_num}"));
        std::fs::write(
            policy_path.join("cpuinfo_max_freq"),
            baseline_max.to_string(),
        )
        .expect("Failed to write to file!");
        std::fs::write(
            policy_path.join("cpuinfo_min_freq"),
            baseline_min.to_string(),
        )
        .expect("Failed to write to file!");

        std::fs::write(policy_path.join("scaling_max_freq"), curr_max.to_string())?;

        std::fs::write(policy_path.join("scaling_min_freq"), curr_min.to_string())?;
        Ok(())
    }

    fn setup_mock_cpu_dev_dirs(root: &Path) -> anyhow::Result<()> {
        fs::create_dir_all(root.join(super::DEVICE_POWER_LIMIT_PATH))?;
        for i in 0..MOCK_NUM_CPU {
            fs::create_dir_all(
                root.join(super::DEVICE_CPUFREQ_PATH)
                    .join(format!("policy{i}")),
            )?;
        }
        Ok(())
    }

    fn helper_read_cpu0_freq_max(root: &Path) -> i32 {
        let policy_path = root.join(super::DEVICE_CPUFREQ_PATH).join("policy0");
        let read_val = std::fs::read(policy_path.join("scaling_max_freq")).unwrap();

        str::from_utf8(&read_val).unwrap().parse::<i32>().unwrap()
    }

    fn helper_read_cpu0_freq_min(root: &Path) -> i32 {
        let policy_path = root.join(super::DEVICE_CPUFREQ_PATH).join("policy0");
        let read_val = std::fs::read(policy_path.join("scaling_min_freq")).unwrap();

        str::from_utf8(&read_val).unwrap().parse::<i32>().unwrap()
    }

    fn setup_mock_files(root: &Path) -> anyhow::Result<()> {
        let pl_files: Vec<&str> = vec![
            "constraint_0_power_limit_uw",
            "constraint_0_max_power_uw",
            "constraint_1_power_limit_uw",
            "constraint_1_max_power_uw",
            "energy_uj",
            "max_energy_range_uj",
        ];

        let cpufreq_files: Vec<&str> = vec!["scaling_max_freq", "cpuinfo_max_freq"];

        for pl_file in &pl_files {
            std::fs::write(
                root.join(super::DEVICE_POWER_LIMIT_PATH)
                    .join(PathBuf::from(pl_file)),
                "0",
            )?;
        }

        for i in 0..MOCK_NUM_CPU {
            let policy_path = root
                .join(super::DEVICE_CPUFREQ_PATH)
                .join(format!("policy{i}"));

            for cpufreq_file in &cpufreq_files {
                std::fs::write(policy_path.join(PathBuf::from(cpufreq_file)), "0")?;
            }
        }

        Ok(())
    }

    #[test]
    fn test_sysfs_files_missing_gives_error() {
        let root = tempdir().unwrap();
        assert!(DeviceCpuStatus::new(PathBuf::from(root.path())).is_err());
    }

    #[test]
    fn test_get_pl0() -> anyhow::Result<()> {
        let root = tempdir()?;
        setup_mock_cpu_dev_dirs(root.path()).unwrap();
        setup_mock_files(root.path()).unwrap();
        write_mock_cpu(root.path(), 0, 3200000, 3000000, 400000, 1000000).unwrap();
        write_mock_pl0(root.path(), 15000000).unwrap();
        let mock_cpu_dev_res = DeviceCpuStatus::new(PathBuf::from(root.path()));
        assert!(mock_cpu_dev_res.is_ok());
        let mock_cpu_dev = mock_cpu_dev_res?;
        assert_eq!(mock_cpu_dev.get_pl0_curr()?, 15000000);

        Ok(())
    }

    #[test]
    fn test_cpu_read_write_reset() -> anyhow::Result<()> {
        let root = tempdir()?;
        setup_mock_cpu_dev_dirs(root.path()).unwrap();
        setup_mock_files(root.path()).unwrap();
        for cpu in 0..MOCK_NUM_CPU {
            write_mock_cpu(root.path(), cpu, 3200000, 3000000, 400000, 1000000).unwrap();
        }

        let mock_cpu_dev_res = DeviceCpuStatus::new(PathBuf::from(root.path()));
        assert!(mock_cpu_dev_res.is_ok());
        let mock_cpu_dev = mock_cpu_dev_res?;
        assert_eq!(helper_read_cpu0_freq_max(root.path()), 3000000);

        mock_cpu_dev.set_all_max_cpu_freq(2000000)?;
        assert_eq!(helper_read_cpu0_freq_max(root.path()), 2000000);

        mock_cpu_dev.set_all_min_cpu_freq(1200000)?;
        assert_eq!(helper_read_cpu0_freq_min(root.path()), 1200000);

        mock_cpu_dev.reset_all_max_min_cpu_freq()?;
        assert_eq!(helper_read_cpu0_freq_max(root.path()), 3000000);
        assert_eq!(helper_read_cpu0_freq_min(root.path()), 1000000);

        mock_cpu_dev.set_all_max_cpu_freq(1600000)?;
        assert_eq!(helper_read_cpu0_freq_max(root.path()), 1600000);

        mock_cpu_dev.set_all_max_cpu_freq(2800000)?;
        assert_eq!(helper_read_cpu0_freq_min(root.path()), 1000000);

        Ok(())
    }
}
