// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod intel_device {
    use crate::{
        common::{self, GameMode},
        cpu_scaling::DeviceCpuStatus,
    };
    use anyhow::{bail, Context, Result};
    use log::{info, warn};
    use regex::Regex;
    use std::{
        fs::{self, File},
        io::{BufRead, BufReader},
        path::PathBuf,
        sync::Mutex,
        thread,
        time::Duration,
    };

    // Device path for cpuinfo.
    const CPUINFO_PATH: &str = "proc/cpuinfo";

    // Device path for GPU card.
    const GPU0_DEVICE_PATH: &str = "sys/class/drm/card0";

    // Expected GPU freq for cometlake.  Used for filtering check.
    const EXPECTED_GPU_MAX_FREQ: u64 = 1000;

    // Guard range when reclocking.  max > min + guard.
    const GPU_FREQUENCY_GUARD_BUFFER_MHZ: u64 = 200;

    pub struct IntelGpuDeviceConfig {
        min_freq_path: PathBuf,

        max_freq_path: PathBuf,

        turbo_freq_path: PathBuf,

        // pub(crate) for sanity unit testing
        /// `power_liit_thr` is a table of tuple containing a power_limit_0 value and
        /// a max_gpu_freq.  Any power_limit that falls within index i and i+1
        /// gets mapped to max_gpu_freq i.  If power limit exceeds index 0, gpu_max_freq
        /// gets mapped to index 0.  Any power_limit below the defined table min will be
        /// mapped to the lowest max_gpu_freq.
        pub(crate) power_limit_thr: Vec<(u64, u64)>,

        polling_interval_ms: u64,
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
            for line in reader.lines().flatten() {
                // Only check CPU0 and fail early.
                // TODO: integrate with `crgoup_x86_64.rs`
                if line.starts_with("vendor_id") {
                    return line.ends_with("GenuineIntel");
                }
            }
        }
        false
    }

    /// Creates a thread that periodically checks for changes in power_limit and adjusts
    /// the GPU frequency accordingly.
    ///
    /// # Arguments
    ///
    /// * `polling_interval_ms` - How often to check if tuning should be re-adjusted
    pub fn run_active_gpu_tuning(polling_interval_ms: u64) -> Result<()> {
        run_active_gpu_tuning_impl(PathBuf::from("/"), polling_interval_ms)
    }

    /// TODO: remove pub. Separate amd and intel unit tests into their own module so
    /// they have access to private functions.  Leave this `pub` for now.
    pub(crate) fn run_active_gpu_tuning_impl(
        root: PathBuf,
        polling_interval_ms: u64,
    ) -> Result<()> {
        static TUNING_RUNNING: Mutex<bool> = Mutex::new(false);

        if let Ok(mut running) = TUNING_RUNNING.lock() {
            if *running {
                // Not an error case since set_game_mode called periodically.
                // Prevent new thread from spawning.
                info!("Tuning thread already running, ignoring new request");
            } else {
                let gpu_dev = IntelGpuDeviceConfig::new(root.to_owned(), polling_interval_ms)?;
                let cpu_dev = DeviceCpuStatus::new(root)?;

                *running = true;

                thread::spawn(move || {
                    info!("Created GPU tuning thread with {polling_interval_ms}ms interval");
                    match gpu_dev.adjust_gpu_frequency(&cpu_dev) {
                        Ok(_) => info!("GPU tuning thread ended successfully"),
                        Err(e) => {
                            warn!("GPU tuning thread ended prematurely: {:?}", e);
                        }
                    }

                    if gpu_dev.tuning_cleanup().is_err() {
                        warn!("GPU tuning thread cleanup failed");
                    }

                    if let Ok(mut running) = TUNING_RUNNING.lock() {
                        *running = false;
                    } else {
                        warn!("GPU Tuning thread Mutex poisoned, unable to reset flag");
                    }
                });
            }
        } else {
            warn!("GPU Tuning thread Mutex poisoned, ignoring run request");
        }

        Ok(())
    }

    impl IntelGpuDeviceConfig {
        /// Create a new Intel GPU device object which can be used to set system tuning parameters.
        ///
        /// # Arguments
        ///
        /// * `root` - root path of device.  Used for using relative paths for testing.  Should
        /// always be '/' for device.
        ///
        /// * `polling_interval_ms` - How often to check if tuning should be re-adjusted.
        ///
        /// # Return
        ///
        /// New Intel GPU device object.
        pub fn new(root: PathBuf, polling_interval_ms: u64) -> Result<IntelGpuDeviceConfig> {
            if !is_intel_device(root.to_owned())
                || !IntelGpuDeviceConfig::is_supported_device(root.to_owned())
            {
                bail!("Not a supported intel device");
            }

            let gpu_dev = IntelGpuDeviceConfig {
                min_freq_path: root.join(GPU0_DEVICE_PATH).join("gt_min_freq_mhz"),
                max_freq_path: root.join(GPU0_DEVICE_PATH).join("gt_max_freq_mhz"),
                turbo_freq_path: root.join(GPU0_DEVICE_PATH).join("gt_boost_freq_mhz"),
                power_limit_thr: vec![
                    (15000000, EXPECTED_GPU_MAX_FREQ),
                    (14500000, 900),
                    (13500000, 800),
                    (12500000, 700),
                    (10000000, 650),
                ],
                polling_interval_ms,
            };

            // Don't attempt to tune if tuning table isn't calibrated for device or another
            // process has already modified the max_freq.
            if gpu_dev.get_gpu_stats()?.max_freq != EXPECTED_GPU_MAX_FREQ {
                bail!("Expected GPU max frequency does not match.  Aborting dynamic tuning.");
            }

            Ok(gpu_dev)
        }

        // This function will only filter in 10th gen (Cometlake CPUs).  The current tuning
        // table is only valid for Intel cometlake deives using a core i3/i5/i7 processors.
        fn is_supported_device(root: PathBuf) -> bool {
            if let Ok(reader) = File::open(root.join(CPUINFO_PATH))
                .map(BufReader::new)
                .context("Couldn't read cpuinfo")
            {
                for line in reader.lines().flatten() {
                    // Only check CPU0 and fail early.
                    if line.starts_with(r"model name") {
                        // Regex will only match 10th gen intel i3, i5, i7
                        // Intel CPU naming convention can be found here:
                        // `https://www.intel.com/content/www/us/en/processors/processor-numbers.html`
                        if let Ok(re) = Regex::new(r".*Intel.* i(3|5|7)-10.*") {
                            return re.is_match(&line);
                        };
                        return false;
                    }
                }
            }
            false
        }

        fn get_gpu_stats(&self) -> Result<GpuStats> {
            Ok(GpuStats {
                min_freq: common::read_file_to_u64(&self.min_freq_path)?,
                max_freq: common::read_file_to_u64(&self.max_freq_path)?,
                _turbo_freq: common::read_file_to_u64(&self.turbo_freq_path)?,
            })
        }

        fn set_gpu_max_freq(&self, val: u64) -> Result<()> {
            Ok(fs::write(&self.max_freq_path, val.to_string())?)
        }

        fn set_gpu_turbo_freq(&self, val: u64) -> Result<()> {
            Ok(fs::write(&self.turbo_freq_path, val.to_string())?)
        }

        /// Function to check the power limit and adjust GPU frequency range.
        /// Function will first check if there any power_limit changes since
        /// the last poll.  If there are changes, it then checks if the power_limit range
        /// has moved to a new bucket, which would require adjusting the GPU
        /// max and turbo frequency.  Buckets are ranges of power_limit values
        /// that map to a specific max_gpu_freq.
        ///
        /// # Arguments
        ///
        /// * `cpu_dev` - CpuDevice object for reading power limit.
        fn adjust_gpu_frequency(&self, cpu_dev: &DeviceCpuStatus) -> Result<()> {
            let mut last_pl_val = cpu_dev.get_pl0_curr()?;
            let mut prev_bucket_index = self.get_pl_bucket_index(last_pl_val);

            while common::get_game_mode()? == GameMode::Borealis {
                thread::sleep(Duration::from_millis(self.polling_interval_ms));

                let current_pl = cpu_dev.get_pl0_curr()?;
                if current_pl == last_pl_val {
                    // No change in powerlimit since last check, no action needed.
                    continue;
                }

                let current_bucket_index = self.get_pl_bucket_index(current_pl);

                // Only change GPU freq if PL0 changed and we moved to a new bucket.
                if current_bucket_index != prev_bucket_index {
                    info!("power_limit_0 changed: {} -> {}", last_pl_val, current_pl);
                    info!(
                        "pl0 bucket changed {} -> {}",
                        prev_bucket_index, current_bucket_index
                    );
                    if let Some(requested_bucket) = self.power_limit_thr.get(current_bucket_index) {
                        let gpu_stats = self.get_gpu_stats()?;
                        let requested_gpu_freq = requested_bucket.1;

                        // This block will assign a new GPU max if needed.  Leave a 200MHz buffer
                        if requested_gpu_freq
                            > (gpu_stats.min_freq + GPU_FREQUENCY_GUARD_BUFFER_MHZ)
                            && requested_gpu_freq != gpu_stats.max_freq
                        {
                            info!("Setting GPU max to {}", requested_gpu_freq);
                            // For the initial version, gpu_max = turbo.
                            self.set_gpu_max_freq(requested_gpu_freq)?;
                            self.set_gpu_turbo_freq(requested_gpu_freq)?;
                        } else {
                            warn!("Did not change GPU frequency to {requested_gpu_freq}");
                        }
                    }
                }
                last_pl_val = current_pl;
                prev_bucket_index = current_bucket_index;
            }

            Ok(())
        }

        // This function returns the index of the vector power_limit_thr where this given
        // power_limit (pl0_val) falls.
        fn get_pl_bucket_index(&self, pl0_val: u64) -> usize {
            for (i, &(pl_thr, _)) in self.power_limit_thr.iter().enumerate() {
                if i == 0 && pl0_val >= pl_thr {
                    // Requested pl0 is bigger than max supported. Use max.
                    return 0;
                } else if i == self.power_limit_thr.len() - 1 {
                    // Didn't fall into any previous bucket.  Use min.
                    return self.power_limit_thr.len() - 1;
                } else if i > 0 && pl0_val > pl_thr {
                    return i;
                }
            }

            // Default is unthrottled (error case)
            0
        }

        pub fn tuning_cleanup(&self) -> Result<()> {
            info!("Active Gpu Tuning STOP requested");

            // Swallow any potential errors when resetting.
            let gpu_max_default = self.power_limit_thr.first().unwrap_or(&(1000, 1000)).1;
            self.set_gpu_max_freq(gpu_max_default)?;
            self.set_gpu_turbo_freq(gpu_max_default)?;

            Ok(())
        }
    }
}

/// Mod for util functions to handle AMD devices.
pub mod amd_device {

    // TODO: removeme once other todos addressed.
    #![allow(dead_code)]

    use anyhow::{bail, Context, Result};
    use regex::Regex;
    use std::fs;
    use std::fs::File;
    use std::io::{BufRead, BufReader};
    use std::path::PathBuf;

    pub struct AmdDeviceConfig {
        /// Device path gpu mode control (auto/manual).
        gpu_mode_path: PathBuf,

        /// Device path for setting system clock.
        sclk_mode_path: PathBuf,
    }

    /// Device path for cpuinfo.
    const CPUINFO_PATH: &str = "/proc/cpuinfo";

    #[derive(Clone, Copy, PartialEq)]
    enum AmdGpuMode {
        /// Auto mode ignores any system clock values.
        Auto,

        /// Manual mode will use any selected system clock value.  If a system clock wasn't explicitly selected, the system will use the last selected value or boot time default.
        Manual,
    }

    impl AmdDeviceConfig {
        /// Creates a new AMD device object which can be used to set system tuning parameters.
        ///
        /// # Arguments
        ///
        /// * `gpu_mode_path` - sysfs path for setting auto/manual control of AMD gpu.  This will typically be at _/sys/class/drm/card0/device/power_dpm_force_performance_level_.
        ///
        /// * `sclk_mode_path` - sysfs path for setting system clock options of AMD gpu.  This will typically be at _/sys/class/drm/card0/device/pp_dpm_sclk_.
        ///
        /// # Return
        ///
        /// New AMD device object.
        pub fn new(gpu_mode_path: &str, sclk_mode_path: &str) -> AmdDeviceConfig {
            AmdDeviceConfig {
                gpu_mode_path: PathBuf::from(gpu_mode_path),
                sclk_mode_path: PathBuf::from(sclk_mode_path),
            }
        }

        /// Static function to check if device has AMU cpu.  Assumes cpuinfo is in _/proc/cpuinfo_.
        ///
        /// # Return
        ///
        /// Boolean denoting if device has AMD CPU.
        pub fn is_amd_device() -> bool {
            println!("amd device check");
            let reader = File::open(PathBuf::from(CPUINFO_PATH))
                .map(BufReader::new)
                .context("Couldn't read cpuinfo");

            match reader {
                Ok(reader_unwrap) => AmdDeviceConfig::has_amd_tag_in_cpu_info(reader_unwrap),
                Err(_) => false,
            }
        }

        // Function split for unit testing
        pub fn has_amd_tag_in_cpu_info<R: BufRead>(reader: R) -> bool {
            // Sample cpuinfo lines:
            // processor        : 0
            // vendor_id        : AuthenticAMD
            // cpu family       : 23
            for line in reader.lines().flatten() {
                // Only check CPU0 and fail early.
                if line.starts_with("vendor_id") {
                    return line.ends_with("AuthenticAMD");
                }
            }
            false
        }

        /// Function checks if CPU family is supported for resourced optimizations.  Checks include:
        ///
        ///     * Ensure system clock options are within expected range.
        ///     * Ensure CPU family is 3rd gen Ryzen 5 or 7 series.
        ///
        /// # Return
        ///
        /// Boolean denoting if device will benefit from manual control of sys clock range.
        pub fn is_supported_device(&self) -> Result<bool> {
            let reader = File::open(PathBuf::from(CPUINFO_PATH))
                .map(BufReader::new)
                .context("Couldn't read cpuinfo")?;

            // TODO: mock out call to unit test function.
            let (sclk_modes, _selected) = self.get_sclk_modes()?;
            if sclk_modes.len() > 2 && (sclk_modes[1] < 600 || sclk_modes[1] > 750) {
                bail!("Unexpected GPU frequency range.  Selected sclk should be between 600MHz-750MHz");
            }

            if self.is_supported_dev_family(reader)? {
                return Ok(true);
            }

            Ok(false)
        }

        // Function split for unit testing.
        pub fn is_supported_dev_family<R: BufRead>(&self, reader: R) -> Result<bool> {
            // Limit scope to 3rd gen ryzen 5/7 series
            // AMD devices don't advertise TDP, so we check cpu model
            // and GPU operating ranges as gating conditions.
            let model_re = Regex::new(r"AMD Ryzen [5|7] 3")?;

            // Sample cpuinfo line:
            // model name       : AMD Ryzen 7 3000C with Radeon Vega Graphics
            for line in reader.lines().flatten() {
                // Only check CPU0 and fail early.
                if line.starts_with("model name") {
                    return Ok(model_re.is_match(&line));
                }
            }
            Ok(false)
        }

        /// Returns array of available sclk modes and the current selection.
        ///
        /// # Return
        ///
        /// Tuple with (`Vector of available system clks`, `currently selected system clk`).
        fn get_sclk_modes(&self) -> Result<(Vec<u32>, u32)> {
            let reader = File::open(PathBuf::from(&self.sclk_mode_path))
                .map(BufReader::new)
                .context("Couldn't read sclk config")?;

            self.parse_sclk(reader)
        }

        // Processing split out for unit testing.
        pub fn parse_sclk<R: BufRead>(&self, reader: R) -> Result<(Vec<u32>, u32)> {
            let mut sclks: Vec<u32> = vec![];
            let mut selected = 0;

            // Sample sclk file:
            // 0: 200Mhz
            // 1: 700Mhz *
            // 2: 1400Mhz
            for line in reader.lines() {
                let line = line?;
                let tokens: Vec<&str> = line.split_whitespace().collect();

                if tokens.len() > 1 {
                    if tokens[1].ends_with("Mhz") {
                        sclks.push(tokens[1].trim_end_matches("Mhz").parse::<u32>()?);
                    } else {
                        bail!("Could not parse sclk.");
                    }
                }

                // Selected frequency is denoted by '*', which adds a 3rd token
                if tokens.len() == 3 && tokens[2] == "*" {
                    selected = tokens[0].trim_end_matches(':').parse::<u32>()?;
                }
            }

            if sclks.is_empty() {
                bail!("No sys clk options found.");
            }

            Ok((sclks, selected))
        }

        /// Sets GPU mode on device (auto or manual).
        fn set_gpu_mode(&self, mode: AmdGpuMode) -> Result<()> {
            let mode_str = if mode == AmdGpuMode::Auto {
                "auto"
            } else {
                "manual"
            };
            fs::write(&self.gpu_mode_path, mode_str)?;
            Ok(())
        }

        /// Sets system clock to requested mode and changes GPU control to manual.
        ///
        /// # Arguments
        ///
        /// * `req_mode` - requested GPU system clock.  This will be an integer mapping to available sclk options.
        fn set_sclk_mode(&self, req_mode: u32) -> Result<()> {
            // Bounds check before trying to set sclk
            let (sclk_modes, selected) = self.get_sclk_modes()?;

            if req_mode < sclk_modes.len() as u32 && req_mode != selected {
                fs::write(&self.sclk_mode_path, req_mode.to_string())?;
                self.set_gpu_mode(AmdGpuMode::Manual)?;
            }
            Ok(())
        }
    }

    /// Init function to setup device, perform validity check, and set GPU control to manual if applicable.
    ///
    /// # Return
    ///
    /// An AMD Device object that can be used to interface with the device.
    pub fn amd_sustained_mode_init() -> Result<AmdDeviceConfig> {
        println!("Setting AMDGPU conf");

        // TODO: add support for multi-GPU.
        let dev: AmdDeviceConfig = AmdDeviceConfig::new(
            "/sys/class/drm/card0/device/power_dpm_force_performance_level",
            "/sys/class/drm/card0/device/pp_dpm_sclk",
        );

        if let Ok(is_supported_dev) = dev.is_supported_device() {
            if is_supported_dev {
                println!("Setting sclk for supported AMD device");
                dev.set_sclk_mode(1)?;
            } else {
                bail!("Unsupported device");
            }
        }

        Ok(dev)
    }

    /// Cleanup function to return GPU to _auto_ mode if applicable.  Will force to auto sclk regardless of initial state or intermediate changes.
    ///
    /// # Arguments
    ///
    /// * `dev` - AMD Device object.
    pub fn amd_sustained_mode_cleanup(dev: &AmdDeviceConfig) {
        match dev.set_gpu_mode(AmdGpuMode::Auto) {
            Ok(_) => println!("GPU mode reset to `AUTO`"),
            Err(_) => println!("Unable to set GPU mode to `AUTO`"),
        }
    }
}

#[cfg(test)]
mod tests {

    use std::{path::PathBuf, thread, time::Duration};
    use tempfile::tempdir;

    use super::{intel_device::IntelGpuDeviceConfig, *};

    use crate::test_utils::tests::*;
    use crate::{
        common, cpu_scaling::DeviceCpuStatus, gpu_freq_scaling::amd_device::AmdDeviceConfig,
    };

    #[test]
    fn test_intel_malformed_root() {
        let _ = IntelGpuDeviceConfig::new(PathBuf::from("/bad_root"), 100).is_err();
    }

    #[test]
    fn test_intel_device_filter() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();

        setup_mock_intel_gpu_dev_dirs(root);
        setup_mock_intel_gpu_files(root);

        // Wrong CPU
        write_mock_cpuinfo(
            root,
            "filter_out",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );
        assert!(IntelGpuDeviceConfig::new(PathBuf::from(root), 100).is_err());

        // Wrong model
        write_mock_cpuinfo(
            root,
            "GenuineIntel",
            "Intel(R) Core(TM) i3-11110U CPU @ 2.10GHz",
        );
        assert!(IntelGpuDeviceConfig::new(PathBuf::from(root), 100).is_err());

        // Supported model
        write_mock_cpuinfo(
            root,
            "GenuineIntel",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );
        assert!(IntelGpuDeviceConfig::new(PathBuf::from(root), 100).is_ok());
    }

    #[test]
    fn test_intel_tuning_table_ordering() {
        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();

        setup_mock_intel_gpu_dev_dirs(root);
        setup_mock_intel_gpu_files(root);
        write_mock_cpuinfo(
            root,
            "GenuineIntel",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );

        let mock_gpu = IntelGpuDeviceConfig::new(PathBuf::from(root), 100).unwrap();

        let mut last_pl_thr: u64 = 0;
        for (i, &(pl_thr, _)) in mock_gpu.power_limit_thr.iter().enumerate() {
            if i == 0 {
                last_pl_thr = pl_thr;
                continue;
            }

            assert!(last_pl_thr > pl_thr);
            last_pl_thr = pl_thr;
        }
    }

    /// TODO: static atomicBool for thread duplication is persisting
    /// in unit test.  Fix before re-enabling
    #[test]
    #[ignore]
    fn test_intel_dynamic_gpu_adjust() {
        const POLLING_DELAY_MS: u64 = 4;
        const OP_LATCH_DELAY_MS: u64 = POLLING_DELAY_MS + 1;

        let power_manager = MockPowerPreferencesManager {};
        assert!(common::get_game_mode().unwrap() == common::GameMode::Off);
        common::set_game_mode(&power_manager, common::GameMode::Borealis).unwrap();
        assert!(common::get_game_mode().unwrap() == common::GameMode::Borealis);

        let tmp_root = tempdir().unwrap();
        let root = tmp_root.path();

        setup_mock_intel_gpu_dev_dirs(root);
        setup_mock_intel_gpu_files(root);
        write_mock_cpuinfo(
            root,
            "GenuineIntel",
            "Intel(R) Core(TM) i3-10110U CPU @ 2.10GHz",
        );

        assert!(IntelGpuDeviceConfig::new(PathBuf::from(root), POLLING_DELAY_MS).is_ok());
        assert!(get_intel_gpu_max(root) == 1000);
        assert!(get_intel_gpu_boost(root) == 1000);

        setup_mock_cpu_dev_dirs(root).unwrap();
        setup_mock_cpu_files(root).unwrap();
        write_mock_pl0(root, 15000000).unwrap();

        // Sanitize CPU object creation (used internally in GPU object)
        let mock_cpu_dev_res = DeviceCpuStatus::new(PathBuf::from(root));
        assert!(mock_cpu_dev_res.is_ok());

        intel_device::run_active_gpu_tuning_impl(root.to_path_buf(), POLLING_DELAY_MS).unwrap();
        // Initial sleep to latch init values
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));

        // Check GPU clock down
        write_mock_pl0(root, 12000000).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 650);
        assert!(get_intel_gpu_boost(root) == 650);

        // Check same bucket, pl0 change
        write_mock_pl0(root, 11000000).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 650);
        assert!(get_intel_gpu_boost(root) == 650);

        // Check PL0 out of range (high)
        write_mock_pl0(root, 18000000).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 1000);
        assert!(get_intel_gpu_boost(root) == 1000);

        // Check PL0 out of range (low)
        write_mock_pl0(root, 8000000).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 650);
        assert!(get_intel_gpu_boost(root) == 650);

        // Check GPU clock up
        write_mock_pl0(root, 14000000).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 800);
        assert!(get_intel_gpu_boost(root) == 800);

        // Check frequency reset on game mode off
        common::set_game_mode(&power_manager, common::GameMode::Off).unwrap();
        thread::sleep(Duration::from_millis(OP_LATCH_DELAY_MS));
        assert!(get_intel_gpu_max(root) == 1000);
        assert!(get_intel_gpu_boost(root) == 1000);
    }

    #[test]
    fn test_amd_device_true() {
        let mock_cpuinfo = construct_poc_cpuinfo_snippet("AuthenticAMD", "dont_care");
        assert!(AmdDeviceConfig::has_amd_tag_in_cpu_info(
            mock_cpuinfo.as_bytes()
        ));
    }

    #[test]
    fn test_amd_device_false() {
        // Incorrect vendor ID
        let mock_cpuinfo = construct_poc_cpuinfo_snippet("GenuineIntel", "dont_care");
        assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
            mock_cpuinfo.as_bytes()
        ));

        // missing vendor ID
        assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
            "".to_string().as_bytes()
        ));
        assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
            "processor: 0".to_string().as_bytes()
        ));
    }

    #[test]
    fn test_amd_parse_sclk_valid() {
        let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

        // trailing space is intentional, reflects sysfs output.
        let mock_sclk = r#"
0: 200Mhz
1: 700Mhz *
2: 1400Mhz "#;

        let (sclk, sel) = dev.parse_sclk(mock_sclk.as_bytes()).unwrap();
        assert_eq!(1, sel);
        assert_eq!(3, sclk.len());
        assert_eq!(200, sclk[0]);
        assert_eq!(700, sclk[1]);
        assert_eq!(1400, sclk[2]);
    }

    #[test]
    fn test_amd_parse_sclk_invalid() {
        let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

        // trailing space is intentional, reflects sysfs output.
        let mock_sclk = r#"
0: nonint
1: 700Mhz *
2: 1400Mhz "#;
        assert!(dev.parse_sclk(mock_sclk.as_bytes()).is_err());
        assert!(dev.parse_sclk("nonint".to_string().as_bytes()).is_err());
        assert!(dev.parse_sclk("0: 1400 ".to_string().as_bytes()).is_err());
        assert!(dev.parse_sclk("0: 1400 *".to_string().as_bytes()).is_err());
        assert!(dev
            .parse_sclk("x: nonint *".to_string().as_bytes())
            .is_err());
    }

    #[test]
    fn test_amd_device_filter_pass() {
        let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

        let mock_cpuinfo = construct_poc_cpuinfo_snippet(
            "AuthenticAMD",
            "AMD Ryzen 7 3700C  with Radeon Vega Mobile Gfx",
        );

        assert!(dev
            .is_supported_dev_family(mock_cpuinfo.as_bytes())
            .unwrap());
        assert!(dev
            .is_supported_dev_family("model name        : AMD Ryzen 5 3700C".as_bytes())
            .unwrap());
    }

    #[test]
    fn test_amd_device_filter_fail() {
        let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

        let mock_cpuinfo = construct_poc_cpuinfo_snippet(
            "AuthenticAMD",
            "AMD Ryzen 3 3700C  with Radeon Vega Mobile Gfx",
        );

        assert!(!dev
            .is_supported_dev_family(mock_cpuinfo.as_bytes())
            .unwrap());
        assert!(!dev
            .is_supported_dev_family("model name        : AMD Ryzen 5 2700C".as_bytes())
            .unwrap());
        assert!(!dev
            .is_supported_dev_family("model name        : AMD Ryzen 3 3700C".as_bytes())
            .unwrap());
        assert!(!dev
            .is_supported_dev_family("model name        : malformed".as_bytes())
            .unwrap());
        assert!(!dev.is_supported_dev_family("".as_bytes()).unwrap());
    }
}
