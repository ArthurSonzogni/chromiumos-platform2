// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use std::fs;
use std::path::{Path, PathBuf};

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use common;

#[derive(Debug)]
pub struct DeviceGPUConfigParams {
    max_freq_path: PathBuf,
    turbo_freq_path: PathBuf,
    power_limit_path: PathBuf,
    // power_limit_thr contains a mapping of power limits to corresponding frequency ranges.
    // Field contains a vector of (power limit, GPU max freq) pairs.
    power_limit_thr: Vec<(u32, u32)>,
}

// TODO: Distinguish RO vs R/W.
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

        cleanup_gpu_scaling();

        println!("Game mode is off");
    });

    Ok(())
}

fn cleanup_gpu_scaling() -> Result<()> {
    println!("Cleanup and reset to defaults");
    // TODO: Needs implementation and sysfs values for default GPU min, max, turbo.
    Ok(())
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
pub fn evaluate_gpu_frequency(config: &DeviceGPUConfigParams, last_pl_val: u32) -> Result<u32> {
    let current_pl = get_sysfs_val(&config, SupportedPaths::PowerLimitCurrent)?;

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
                prev_thr = pl_thr.clone();
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
        && requested_gpu_freq != get_sysfs_val(&config, SupportedPaths::GpuMax)?
    {
        println!("Setting GPU max to {}", requested_gpu_freq);
        // For the initial version, gpu_max = turbo.
        set_sysfs_val(&config, SupportedPaths::GpuMax, requested_gpu_freq)?;
        set_sysfs_val(&config, SupportedPaths::GpuTurbo, requested_gpu_freq)?;
    }

    Ok(current_pl)
}

/// Gathers and bundles device-specific configurations related to GPU frequency and power limit.
pub fn init_gpu_params() -> Result<DeviceGPUConfigParams> {
    let config = DeviceGPUConfigParams {
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
    config: &DeviceGPUConfigParams,
    path_type: SupportedPaths,
) -> Result<PathBuf> {
    let path;
    match path_type {
        SupportedPaths::GpuMax => path = &config.max_freq_path,
        SupportedPaths::GpuTurbo => path = &config.turbo_freq_path,
        SupportedPaths::PowerLimitCurrent => path = &config.power_limit_path,
        _ => bail!("Unsupported path type."),
    }

    Ok(PathBuf::from(path))
}

// TODO: Move the R/W functions to traits to allow mocking and stubbing for unit testing.
fn get_sysfs_val(config: &DeviceGPUConfigParams, path_type: SupportedPaths) -> Result<u32> {
    let path_buf = get_supported_path(&config, path_type)?;
    Ok(common::read_file_to_u64(path_buf.as_path())? as u32)
}

fn set_sysfs_val(
    config: &DeviceGPUConfigParams,
    path_type: SupportedPaths,
    val: u32,
) -> Result<()> {
    let path_buf = get_supported_path(&config, path_type)?;
    let path = path_buf.as_path();

    match Path::new(path).exists() {
        true => {
            fs::write(path, val.to_string())?;
            Ok(())
        }
        false => bail!("Could not write to path: {:?}", path.to_str()),
    }
}
