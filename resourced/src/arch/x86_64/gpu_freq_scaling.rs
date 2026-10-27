// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod intel_device {
    use std::fs;
    use std::fs::File;
    use std::io::BufRead;
    use std::io::BufReader;
    use std::path::Path;
    use std::path::PathBuf;

    use anyhow::bail;
    use anyhow::Context;
    use anyhow::Result;
    use log::info;

    use crate::common::read_from_file;

    // Device path for cpuinfo.
    const CPUINFO_PATH: &str = "proc/cpuinfo";

    // Device path for GPU card.
    const GPU0_DEVICE_PATH: &str = "sys/class/drm/card0";

    // Relative path of Request-based Power Saving boost frequency.
    const RPS_BOOST_FREQ_PATH: &str = "gt/gt0/rps_boost_freq_mhz";

    pub struct IntelGpuDeviceConfig {
        min_freq_path: PathBuf,

        max_freq_path: PathBuf,

        turbo_freq_path: PathBuf,
    }

    struct GpuStats {
        min_freq: u64,

        max_freq: u64,

        // Turbo freq is not manually controlled.  MAX == TURBO initially.
        _turbo_freq: u64,
    }

    /// Function to check if device has Intel cpu.
    ///
    /// # Return
    ///
    /// Boolean denoting if device has Intel CPU.
    pub fn is_intel_device(root: PathBuf) -> bool {
        if let Ok(reader) = File::open(root.join(CPUINFO_PATH))
            .map(BufReader::new)
            .context("Couldn't read cpuinfo")
        {
            for line in reader.lines().map_while(Result::ok) {
                // Only check CPU0 and fail early.
                // TODO: integrate with `crgoup_x86_64.rs`
                if line.starts_with("vendor_id") {
                    return line.ends_with("GenuineIntel");
                }
            }
        }
        false
    }

    /// Checks if kernel offers RPS (Request-based Power Savings).
    ///
    /// # Returns
    ///
    /// True if kernel offers RPS, false otherwise.
    fn has_rps(root: &Path) -> bool {
        fs::metadata(root.join(GPU0_DEVICE_PATH).join(RPS_BOOST_FREQ_PATH)).is_ok()
    }

    impl IntelGpuDeviceConfig {
        /// Create a new Intel GPU device object which can be used to set system tuning parameters.
        ///
        /// # Arguments
        ///
        /// * `root` - root path of device.  Used for using relative paths for testing.  Should
        ///   always be '/' for device.
        ///
        /// # Return
        ///
        /// New Intel GPU device object.
        pub fn new(root: PathBuf) -> Result<IntelGpuDeviceConfig> {
            if !is_intel_device(root.to_owned()) {
                bail!("Not an intel device");
            }

            let gpu_dev = IntelGpuDeviceConfig {
                min_freq_path: root.join(GPU0_DEVICE_PATH).join("gt_min_freq_mhz"),
                max_freq_path: root.join(GPU0_DEVICE_PATH).join("gt_max_freq_mhz"),
                turbo_freq_path: if has_rps(&root) {
                    info!("using RPS boost sysfs entry");
                    root.join(GPU0_DEVICE_PATH).join(RPS_BOOST_FREQ_PATH)
                } else {
                    info!("using legacy boost sysfs entry");
                    root.join(GPU0_DEVICE_PATH).join("gt_boost_freq_mhz")
                },
            };

            Ok(gpu_dev)
        }

        /// Sets the RTC audio active state.
        ///
        /// # Arguments
        ///
        /// * `active` - The new RTC audio active state.
        ///
        /// # Returns
        ///
        /// A `Result` with an `Ok` value if the state was successfully set, or
        /// a `Err` value with an error message if the state could not be set.
        pub fn set_rtc_audio_active(&mut self, active: bool) -> Result<()> {
            let gpu_stats = self.get_gpu_stats()?;
            if gpu_stats._turbo_freq == 0 {
                bail!("cannot change gt_boost_freq_mhz when it's 0")
            }
            if active {
                self.set_gpu_turbo_freq(gpu_stats.min_freq)
            } else {
                self.set_gpu_turbo_freq(gpu_stats.max_freq)
            }
        }

        fn get_gpu_stats(&self) -> Result<GpuStats> {
            Ok(GpuStats {
                min_freq: read_from_file(&self.min_freq_path)?,
                max_freq: read_from_file(&self.max_freq_path)?,
                _turbo_freq: read_from_file(&self.turbo_freq_path)?,
            })
        }

        fn set_gpu_turbo_freq(&self, val: u64) -> Result<()> {
            Ok(fs::write(&self.turbo_freq_path, val.to_string())?)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::intel_device::IntelGpuDeviceConfig;

    #[test]
    fn test_intel_malformed_root() {
        let _ = IntelGpuDeviceConfig::new(PathBuf::from("/bad_root")).is_err();
    }
}
