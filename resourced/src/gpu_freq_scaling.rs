// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: removeme once other todos addressed.
#![allow(dead_code)]

use anyhow::{bail, Result};
use std::fs;
use std::path::{Path, PathBuf};

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use crate::gpu_freq_scaling::amd_device::{
    amd_sustained_mode_cleanup, amd_sustained_mode_init, AmdDeviceConfig,
};

use crate::common;

#[derive(Debug)]
pub struct DeviceGpuConfigParams {
    max_freq_path: PathBuf,
    turbo_freq_path: PathBuf,
    power_limit_path: PathBuf,
    // power_limit_thr contains a mapping of power limits to corresponding frequency ranges.
    // Field contains a vector of (power limit, GPU max freq) pairs.
    power_limit_thr: Vec<(u32, u32)>,
}

// TODO: Distinguish RO vs R/W.
#[allow(dead_code)]
enum SupportedPaths {
    GpuMax,
    GpuTurbo,
    PowerLimitCurrent,
}

/// Starts a thread that continuously monitors power limits and adjusts gpu frequency accordingly.
///
/// This function will spawn a thread that will periodically poll the power limit and adjust
/// GPU frequency as needed.  The thread can be terminated by the caller by setting the game_mode_on
/// atomic bool to false.  There can be a delay of up to 1x polling_freq_ms to clean up the thread.
///
/// # Arguments
///
/// * `game_mode_on` - A shared atomic bool with the caller that controls when the
///                    newly spawned thread will terminate.
///
/// * `polling_freq_ms` - The polling frequency to use for reading the power limit val.
///                       1000ms can be used as a default.
///
/// # Return
///
/// Result enum indicated successful creation of thread.
pub fn init_gpu_scaling_thread(game_mode_on: Arc<AtomicBool>, polling_freq_ms: u64) -> Result<()> {
    let config = init_gpu_params()?;
    let mut last_pl_val: u32 = 15000000;

    thread::spawn(move || {
        let mut consecutive_fails: u32 = 0;

        let amd_dev: Option<AmdDeviceConfig> = if AmdDeviceConfig::is_amd_device() {
            match amd_sustained_mode_init() {
                Ok(dev) => Some(dev),
                Err(_) => return,
            }
        } else {
            None
        };

        while game_mode_on.load(Ordering::Relaxed) && consecutive_fails < 100 {
            println!("Game mode ON");

            let last_pl_buf = evaluate_gpu_frequency(&config, last_pl_val);
            match last_pl_buf {
                Ok(pl) => {
                    last_pl_val = pl;
                    consecutive_fails = 0;
                }
                Err(_) => consecutive_fails += 1,
            }

            thread::sleep(Duration::from_millis(polling_freq_ms));
        }

        if consecutive_fails >= 100 {
            println!("Gpu scaling failed");
        }

        // Cleanup
        if let Some(dev) = amd_dev {
            amd_sustained_mode_cleanup(&dev);
        } else {
            cleanup_gpu_scaling();
        }

        println!("Game mode is off");
    });

    Ok(())
}

fn cleanup_gpu_scaling() {
    println!("Cleanup and reset to defaults");
    // TODO: Needs implementation and sysfs values for default GPU min, max, turbo.
}

/// Stateless function to check the power limit and adjust GPU frequency range.
///
/// This function is called periodically to check if GPU frequency range needs
/// to be adjusted.  Providing a last_pl_val allows it to check for deltas in
/// power limit.
///
/// # Arguments
///
/// * `config` - Device-specifc configuration files to use.
///
/// * `last_pl_val` - Previously buffered power limit value.
///
/// # Return u32 value
///
/// Returns the current power limit.  Can be buffered and sent in the subsequent call.
pub fn evaluate_gpu_frequency(config: &DeviceGpuConfigParams, last_pl_val: u32) -> Result<u32> {
    let current_pl = get_sysfs_val(config, SupportedPaths::PowerLimitCurrent)?;

    println!("Last PL =\t {}\nCurrent PL =\t {}", last_pl_val, current_pl);
    if current_pl == last_pl_val {
        // No change in powerlimit since last check, no action needed.
        return Ok(current_pl);
    }

    let mut prev_thr = 0;
    let mut requested_gpu_freq = 0;
    let mut last_bucket = false;

    // This loop will iterate over the power limit threshold to GPU max frequency mappings
    // provided in the config param.  If the current_pl falls within the range of the any 2
    // threshold ranges (prev_thr and pl_thr), attempt to assign the corresponding max GPU freq.
    // Additionally checks to see if the previously buffered power_limit value (last_pl_val) was
    // in a different bucket compared to the current power limit value (current_pl).
    for (i, &(pl_thr, gpu_max)) in config.power_limit_thr.iter().enumerate() {
        println!(
            "Checking config: pl_thr = {}, gpu_max = {}",
            pl_thr, gpu_max
        );
        if i == 0 {
            if current_pl >= pl_thr {
                // This case should never happen, but assign Max in case.
                requested_gpu_freq = gpu_max;
                break;
            } else {
                prev_thr = pl_thr;
            }

            continue;
        } else if i == (config.power_limit_thr.len() - 1) {
            last_bucket = true;
        }

        // If threshold is within a bucket range or below the min bucket.
        if prev_thr >= current_pl && current_pl > pl_thr || (last_bucket && current_pl < pl_thr) {
            println!("Current pl thr in bucket = {}", gpu_max);

            // This check might be unnecessary.  Read guard on write protects unnecessary override already.
            if prev_thr > last_pl_val && last_pl_val > pl_thr
                || (last_bucket && last_pl_val < pl_thr)
            {
                // Still in same bucket, no change in GPU freq.
                return Ok(current_pl);
            }

            // PL is in new threshold bucket, adjust max freq.
            requested_gpu_freq = gpu_max;
            break;
        }
    }

    // TODO: Compare against GPU_MIN+buffer instead of 0.  Need to read GPU min from sysfs.
    // This block will assign a new GPU max if needed.
    if requested_gpu_freq > 0
        && requested_gpu_freq != get_sysfs_val(config, SupportedPaths::GpuMax)?
    {
        println!("Setting GPU max to {}", requested_gpu_freq);
        // For the initial version, gpu_max = turbo.
        set_sysfs_val(config, SupportedPaths::GpuMax, requested_gpu_freq)?;
        set_sysfs_val(config, SupportedPaths::GpuTurbo, requested_gpu_freq)?;
    }

    Ok(current_pl)
}

/// Gathers and bundles device-specific configurations related to GPU frequency and power limit.
pub fn init_gpu_params() -> Result<DeviceGpuConfigParams> {
    let config = DeviceGpuConfigParams {
        max_freq_path: PathBuf::from("/sys/class/drm/card0/gt_max_freq_mhz"),
        turbo_freq_path: PathBuf::from("/sys/class/drm/card0/gt_boost_freq_mhz"),
        power_limit_path: PathBuf::from(
            "/sys/class/powercap/intel-rapl/intel-rapl:0/constraint_0_power_limit_uw",
        ),
        //TODO: power_limit_thr must be guaranteed pre-sorted
        //      Need to get TDP max (/sys/class/powercap/intel-rapl/intel-rapl:0/constraint_0_max_power_uw),
        //      GPU max/min from sysfs; or read tuning from a device config file
        power_limit_thr: vec![
            (15000000, 1000),
            (14500000, 900),
            (13500000, 800),
            (12500000, 700),
            (10000000, 650),
        ],
    };

    Ok(config)
}

fn get_supported_path(
    config: &DeviceGpuConfigParams,
    path_type: SupportedPaths,
) -> Result<PathBuf> {
    let path;
    match path_type {
        SupportedPaths::GpuMax => path = &config.max_freq_path,
        SupportedPaths::GpuTurbo => path = &config.turbo_freq_path,
        SupportedPaths::PowerLimitCurrent => path = &config.power_limit_path,
    }

    Ok(PathBuf::from(path))
}

// TODO: Move the R/W functions to traits to allow mocking and stubbing for unit testing.
fn get_sysfs_val(config: &DeviceGpuConfigParams, path_type: SupportedPaths) -> Result<u32> {
    let path_buf = get_supported_path(config, path_type)?;
    Ok(common::read_file_to_u64(path_buf.as_path())? as u32)
}

fn set_sysfs_val(
    config: &DeviceGpuConfigParams,
    path_type: SupportedPaths,
    val: u32,
) -> Result<()> {
    let path_buf = get_supported_path(config, path_type)?;
    let path = path_buf.as_path();

    match Path::new(path).exists() {
        true => {
            fs::write(path, val.to_string())?;
            Ok(())
        }
        false => bail!("Could not write to path: {:?}", path.to_str()),
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
            // processor	: 0
            // vendor_id	: AuthenticAMD
            // cpu family	: 23
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
            // model name	: AMD Ryzen 7 3000C with Radeon Vega Graphics
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
