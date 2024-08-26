// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::time::Duration;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use log::debug;
use log::error;
use log::info;
use log::warn;
use once_cell::sync::OnceCell;
use tokio::sync::Notify;
use tokio::time::Instant;

// Configuration parameters import
use super::auto_epp_config::Epp;
use super::auto_epp_config::ThresholdsIntel;
use super::auto_epp_config::TimeConstantsIntel;
use super::auto_epp_config::MAX_CONSECUTIVE_ERRORS;
use super::auto_epp_config::PREVENT_OVERTURBO;
// globals
use super::globals::BSM_SIGNAL;
use super::globals::DYNAMIC_EPP;
use super::globals::MEDIA_CGROUP_SIGNAL;
use super::globals::RTC_FS_SIGNAL;
use super::platform::IS_MTL;
use crate::cpu_utils::write_to_cpu_policy_patterns;

// File path
const STAT_FILE_PATH: &str = "/proc/stat";
const GPU_RC6_PATH: &str = "/sys/class/drm/card0/gt/gt0/rc6_residency_ms";
const EPP_PATH: &str = "/sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference";

// Feature flag
use crate::feature;

const FEATURE_DYNAMIC_EPP: &str = "CrOSLateBootDynamicEPP";
// Notification used for waking up the co-routine on flag change.
static DYNAMIC_EPP_FLAG_CHANGED: OnceCell<Notify> = OnceCell::new();

// Register Dynamic Epp feature from dbus.rs
pub fn init() {
    if *IS_MTL {
        const DEFAULT_STATE: bool = true;
        feature::register_feature(
            FEATURE_DYNAMIC_EPP,
            DEFAULT_STATE,
            Some(Box::new(dynamic_epp_cb)),
        );
        DYNAMIC_EPP_FLAG_CHANGED
            .set(Notify::new())
            .expect("Double initialization of DYNAMIC_EPP_FLAG_CHANGED");
        dynamic_epp_cb(DEFAULT_STATE);
        // Spawn the main coroutine.
        tokio::spawn(async move {
            auto_epp_main().await;
        });
    }
}

fn dynamic_epp_cb(new_state: bool) {
    DYNAMIC_EPP.set_value(new_state);
    DYNAMIC_EPP_FLAG_CHANGED
        .get()
        .expect("DYNAMIC_EPP_FLAG_CHANGED is not initialized")
        .notify_one();
    if new_state {
        info!("Dynamic EPP is enabled");
    } else {
        info!("Dynamic EPP is disabled");
    }
}

fn set_epp_for_all_cores(epp_value: u32) {
    if let Err(err) = write_to_cpu_policy_patterns(EPP_PATH, &epp_value.to_string()) {
        error!("Failed to set EPP for all cores: {}", err);
    } else {
        debug!("EPP set successfully for all cores: {}", epp_value);
    }
}

// Calculate exponential moving average (EMA)
fn calculate_ema(prev_ema: f64, current_value: f64) -> f64 {
    TimeConstantsIntel::alpha() * current_value + (1.0 - TimeConstantsIntel::alpha()) * prev_ema
}

// Calculate GPU RC6 residency
async fn calculate_rc6_residency(file_path: &str, max_residency_ms: Duration) -> Result<f64> {
    let file_cur =
        File::open(file_path).with_context(|| format!("Failed to open file: {}", file_path))?;
    let reader_cur = BufReader::new(file_cur);

    // Read current residency
    let current_residency: u64 = if let Some(Ok(line)) = reader_cur.lines().next() {
        line.trim()
            .parse()
            .with_context(|| "Failed to parse current residency")?
    } else {
        bail!("Missing current_residency line");
    };

    tokio::time::sleep(max_residency_ms).await;

    // Read new residency
    let file_new =
        File::open(file_path).with_context(|| format!("Failed to open file: {}", file_path))?;
    let reader_new = BufReader::new(file_new);
    let new_residency: u64 = if let Some(Ok(line)) = reader_new.lines().next() {
        line.trim()
            .parse()
            .with_context(|| "Failed to parse new residency")?
    } else {
        bail!("Missing new residency line");
    };

    let delta_residency = new_residency
        .checked_sub(current_residency)
        .ok_or_else(|| anyhow::Error::msg("Overflow when calculating residency delta"))?;
    let residency_percentage =
        ((delta_residency as f64) / (max_residency_ms.as_millis() as f64)) * 100.0;
    let capped_percentage = residency_percentage.min(100.0);
    Ok(capped_percentage)
}

#[derive(Clone, Default, Debug)]
struct CpuStats {
    total: u64,
    idle: u64,
    cpu_index: usize,
}

fn parse_cpu_stats(line: &str) -> Option<CpuStats> {
    if !line.starts_with("cpu") {
        return None;
    }

    let fields: Vec<&str> = line.split_whitespace().collect();

    if let Some(cpu_index_str) = fields[0].strip_prefix("cpu") {
        if let Ok(cpu_index) = cpu_index_str.parse::<usize>() {
            let user = fields[1].parse::<u64>().unwrap_or(0);
            let nice = fields[2].parse::<u64>().unwrap_or(0);
            let system = fields[3].parse::<u64>().unwrap_or(0);
            let idle = fields[4].parse::<u64>().unwrap_or(0);

            // Calculate total CPU time
            let total = user + nice + system + idle;

            Some(CpuStats {
                total,
                idle,
                cpu_index,
            })
        } else {
            None
        }
    } else {
        None
    }
}

// Use a structure to reduce the number of args passsed to
// read_and_parse_cpu_stats
struct CpuStatsContext<'data> {
    prev_cpu_stats: &'data mut Vec<CpuStats>,
    cpu_usages: &'data mut Vec<f64>,
    ema_values: &'data mut Vec<f64>,
    high_utilization_cores: &'data mut Vec<usize>,
    moderate_utilization_cores: &'data mut Vec<usize>,
}

fn read_and_parse_cpu_stats(
    context: &mut CpuStatsContext,
    threshold_high: f64,
    threshold_moderate: f64,
    stat_file_path: &str,
) -> Result<()> {
    let file = std::fs::File::open(stat_file_path).context("Failed to open /proc/stat file")?;
    let reader = std::io::BufReader::new(file);

    // Iterate through each line of /proc/stat
    // Skip the first line which contains overall CPU stats
    for line in reader.lines().skip(1) {
        let line = line.context("Error reading line")?;

        let cpu_stats = if let Some(cpu_stats) = parse_cpu_stats(&line) {
            cpu_stats
        } else {
            break;
        };

        let cpu_index = cpu_stats.cpu_index;

        // Check if the context vectors need resizing
        if cpu_index >= context.prev_cpu_stats.len() {
            debug!("Resizing vectors for CPU Index: {}", cpu_index);
            context
                .prev_cpu_stats
                .resize(cpu_index + 1, CpuStats::default());
            context.cpu_usages.resize(cpu_index + 1, 0.0);
            context.ema_values.resize(cpu_index + 1, 0.0);
        }

        let prev_stats = &mut context.prev_cpu_stats[cpu_index];

        // Checks to handle cases where total delta is zero
        let total_delta = cpu_stats.total.checked_sub(prev_stats.total).unwrap_or(1);
        let idle_delta = cpu_stats.idle.saturating_sub(prev_stats.idle);

        if total_delta > 0 {
            let cpu_usage = (1.0 - (idle_delta as f64) / (total_delta as f64))
                .max(0.0)
                .min(1.0)
                * 100.0;

            debug!("Core {}: CPU Usage: {:.2}%", cpu_index, cpu_usage);

            // Ensure the vector is large enough to accommodate the CPU index
            if cpu_index >= context.cpu_usages.len() {
                context.cpu_usages.resize(cpu_index + 1, 0.0);
            }

            context.cpu_usages[cpu_index] = cpu_usage;

            *prev_stats = cpu_stats;

            // Calculate Exponential Moving Average (EMA)
            context.ema_values[cpu_index] = calculate_ema(context.ema_values[cpu_index], cpu_usage);

            // Determine high and moderate utilization
            // based on per-core EMA with hysteresis
            if context.ema_values[cpu_index] > threshold_high {
                context.high_utilization_cores.push(cpu_index);
            }
            if context.ema_values[cpu_index] < threshold_high
                && context.ema_values[cpu_index] > threshold_moderate
            {
                context.moderate_utilization_cores.push(cpu_index);
            }
        }
    }

    Ok(())
}

struct EppCalculationArgs<'data> {
    cpu_usages: &'data [f64],
    all_below_threshold_moderate_start: &'data mut Option<Instant>,
    high_utilization_cores: &'data [usize],
    moderate_utilization_cores: &'data [usize],
    gpu_util_high: bool,
    rtc_fs_value: bool,
    bsm_state: bool,
    media_cgroup_state: bool,
    threshold_moderate: f64,
}

fn calculate_epp_duration(args: EppCalculationArgs) -> (Option<u32>, Duration) {
    let EppCalculationArgs {
        cpu_usages,
        all_below_threshold_moderate_start,
        high_utilization_cores,
        moderate_utilization_cores,
        gpu_util_high,
        rtc_fs_value,
        bsm_state,
        media_cgroup_state,
        threshold_moderate,
    } = args;

    // Feature to prevent 50% cores bursting at same time
    let all_above_threshold_flag = if PREVENT_OVERTURBO {
        let potential_overturbo_threshold = ThresholdsIntel::prevent_overturbo();
        let total_cores = num_cpus::get();
        cpu_usages
            .iter()
            .filter(|&&usage| usage > potential_overturbo_threshold)
            .count()
            > total_cores / 2
    } else {
        false
    };

    let (new_epp, epp_duration) = if !high_utilization_cores.is_empty()
        && !gpu_util_high
        && !rtc_fs_value
        && !bsm_state
        && !media_cgroup_state
    {
        let new_epp = if all_above_threshold_flag {
            Some(Epp::epp_allcore())
        } else {
            Some(Epp::epp_perf())
        };
        *all_below_threshold_moderate_start = None;
        (new_epp, TimeConstantsIntel::high_threshold_time())
    } else {
        let new_epp = if !moderate_utilization_cores.is_empty() || gpu_util_high {
            if media_cgroup_state {
                Some(Epp::epp_eff())
            } else {
                Some(Epp::epp_bal())
            }
        } else if cpu_usages.iter().all(|&usage| usage < threshold_moderate) {
            let start = all_below_threshold_moderate_start.get_or_insert_with(Instant::now);
            let elapsed_time = start.elapsed();
            debug!("elapsed_time:{:?}", elapsed_time);
            if elapsed_time.as_nanos() >= TimeConstantsIntel::low_threshold_time().as_nanos()
                && !gpu_util_high
            {
                Some(Epp::epp_eff())
            } else {
                None
            }
        } else {
            *all_below_threshold_moderate_start = None;
            // Check if any core utilization is above threshold_moderate
            let any_above_threshold_mod =
                cpu_usages.iter().any(|&usage| usage > threshold_moderate);

            if any_above_threshold_mod && !media_cgroup_state {
                // Exit EPP Efficiency and set EPP BAL if any core is above threshold_moderate
                Some(Epp::epp_bal())
            } else {
                None
            }
        };
        (new_epp, TimeConstantsIntel::cpu_sampling_time())
    };

    (new_epp, epp_duration)
}

// TODO: Add media and NPU metrics
pub async fn auto_epp_main() {
    // Track errors
    let mut parse_errors = 0;
    // Duration to track how long all cores are below threshold_moderate
    let mut all_below_threshold_moderate_start = None;
    let mut epp_tracking: u32 = Epp::epp_default();

    // Number of CPU cores in the system
    // SAFETY: Safe because syscall doesn't modify user memory
    let num_cores = match unsafe { libc::sysconf(libc::_SC_NPROCESSORS_CONF) } {
        -1 => {
            warn!("Dynamic EPP: Failed to get the number of CPU cores. Exiting auto_epp_main");
            return;
        }
        n => n as usize,
    };

    let mut prev_cpu_stats: Vec<CpuStats> = vec![Default::default(); num_cores];
    let mut ema_values: Vec<f64> = vec![Default::default(); num_cores];

    loop {
        // Terminating Auto EPP
        let terminate_flag = !DYNAMIC_EPP.read_value();

        // If Dynamic EPP is disabled in runtime...
        if terminate_flag {
            // ...ensure the default EPP is set...
            set_epp_for_all_cores(Epp::epp_default());
            // ...park the coroutine and wait until it's needed again.
            DYNAMIC_EPP_FLAG_CHANGED
                .get()
                .expect("DYNAMIC_EPP_FLAG_CHANGED is not initialized")
                .notified()
                .await;
            continue;
        }

        // Initialize CPU vectors
        let mut cpu_usages: Vec<f64> = Vec::with_capacity(num_cores);
        let mut high_utilization_cores: Vec<usize> = Vec::with_capacity(num_cores);
        let mut moderate_utilization_cores: Vec<usize> = Vec::with_capacity(num_cores);

        // RTC and FS signal
        let rtc_fs_value = RTC_FS_SIGNAL.read_value();

        // Battery Saver Mode Signal
        let bsm_state = BSM_SIGNAL.read_value();

        // Media C-group Signal
        let media_cgroup_state = MEDIA_CGROUP_SIGNAL.read_value();

        // Resize vectors based on the current number of CPU cores
        prev_cpu_stats.resize_with(num_cores, Default::default);
        ema_values.resize_with(num_cores, Default::default);

        let mut context = CpuStatsContext {
            prev_cpu_stats: &mut prev_cpu_stats,
            cpu_usages: &mut cpu_usages,
            ema_values: &mut ema_values,
            high_utilization_cores: &mut high_utilization_cores,
            moderate_utilization_cores: &mut moderate_utilization_cores,
        };

        let (threshold_high, threshold_moderate) = if rtc_fs_value {
            (
                ThresholdsIntel::rtc_fs_high(),
                ThresholdsIntel::rtc_fs_mod(),
            )
        } else {
            (
                ThresholdsIntel::normal_high(),
                ThresholdsIntel::normal_mod(),
            )
        };

        if let Err(err) = read_and_parse_cpu_stats(
            &mut context,
            threshold_high,
            threshold_moderate,
            STAT_FILE_PATH,
        ) {
            parse_errors += 1;
            warn!("Error in reading and parsing CPU stats: {}", err);
            if parse_errors >= MAX_CONSECUTIVE_ERRORS {
                warn!("Consecutive errors in parsing CPU stats. Terminating Auto EPP");
                return;
            }
            // Skip calculations
            continue;
        } else {
            parse_errors = 0;
        }
        // Calculate GPU residency
        let gpu_residency_percentage_result =
            calculate_rc6_residency(GPU_RC6_PATH, TimeConstantsIntel::gpu_max_residency()).await;

        let gpu_util_high = match gpu_residency_percentage_result {
            Ok(residency_percentage) => {
                debug!("RC6 residency: {:.2}%", residency_percentage);
                residency_percentage < ThresholdsIntel::gpu_rc6()
            }
            Err(err) => {
                warn!("Error calculating RC6 residency: {}", err);
                let new_epp = Epp::epp_default();
                if new_epp != epp_tracking {
                    set_epp_for_all_cores(new_epp);
                }
                epp_tracking = new_epp;
                tokio::time::sleep(TimeConstantsIntel::cpu_sampling_time()).await;
                continue;
            }
        };
        debug!("gpu_util_high: {}", gpu_util_high);

        let (new_epp, epp_duration) = calculate_epp_duration(EppCalculationArgs {
            cpu_usages: &cpu_usages,
            all_below_threshold_moderate_start: &mut all_below_threshold_moderate_start,
            high_utilization_cores: &high_utilization_cores,
            moderate_utilization_cores: &moderate_utilization_cores,
            gpu_util_high,
            rtc_fs_value,
            bsm_state,
            media_cgroup_state,
            threshold_moderate,
        });

        if let Some(new_epp) = new_epp {
            if new_epp != epp_tracking {
                set_epp_for_all_cores(new_epp);
            }
            epp_tracking = new_epp;
        }
        // Sleep before waking up the thread for auto EPP
        tokio::time::sleep(epp_duration).await;
    }
}

#[cfg(test)]
mod tests {
    use std::fs::File;
    use std::io::Write;

    use tempfile::NamedTempFile;
    use tokio::time::sleep;
    use tokio::time::Duration;
    use tokio::time::Instant;

    use super::calculate_epp_duration;
    use super::calculate_rc6_residency;
    use super::parse_cpu_stats;
    use super::read_and_parse_cpu_stats;
    use super::CpuStats;
    use super::CpuStatsContext;
    use super::EppCalculationArgs;
    use crate::arch::x86_64::auto_epp_config::Epp;
    use crate::arch::x86_64::auto_epp_config::TimeConstantsIntel;

    const GPU_RC6_PATH_1: &str = "/tmp/gpu_rc6_residency_1.txt";

    #[tokio::test]
    async fn test_calculate_rc6_residency() -> Result<(), anyhow::Error> {
        create_temp_file(GPU_RC6_PATH_1, "500\n")?;

        // Spawn a background task to update the residency to 1000
        tokio::spawn(async {
            sleep(Duration::from_millis(500)).await;
            update_temp_file(GPU_RC6_PATH_1, "1000\n")?;
            Ok::<(), std::io::Error>(())
        });

        let result = calculate_rc6_residency(GPU_RC6_PATH_1, Duration::from_millis(1000)).await;

        assert_eq!(result?, 50.0);

        Ok(())
    }

    fn create_temp_file(file_path: &str, content: &str) -> Result<(), std::io::Error> {
        let mut temp_file = File::create(file_path)?;
        temp_file.write_all(content.as_bytes())?;
        Ok(())
    }

    fn update_temp_file(file_path: &str, content: &str) -> Result<(), std::io::Error> {
        let mut temp_file = File::create(file_path)?;
        temp_file.write_all(content.as_bytes())?;
        Ok(())
    }
    // GPU test case end

    #[test]
    fn test_parse_cpu_stats() {
        let line = "cpu0 10 20 30 40 50";

        if let Some(cpu_stats) = parse_cpu_stats(line) {
            assert_eq!(cpu_stats.total, 100);
            assert_eq!(cpu_stats.idle, 40);
            assert_eq!(cpu_stats.cpu_index, 0);
        } else {
            panic!("Failed to parse CPU stats for line: {}", line);
        }
    }

    #[test]
    fn test_read_and_parse_cpu_stats() -> Result<(), anyhow::Error> {
        // Create a temporary file with sample CPU stats
        let file = NamedTempFile::new()?;
        writeln!(
            &file,
            "cpu  100 200 300 400 500 600 700 800 900 1000\n\
            cpu0 10 20 30 40 50 60 70 80 90 100\n\
            cpu1 90 30 40 50 60 70 80 90 100 110\n\
            cpu2 30 40 50 60 70 80 90 100 110 120"
        )?;

        // Create an instance of CpuStatsContext
        let mut context = CpuStatsContext {
            prev_cpu_stats: &mut vec![
                CpuStats {
                    cpu_index: 0,
                    total: 0,
                    idle: 0,
                },
                CpuStats {
                    cpu_index: 1,
                    total: 0,
                    idle: 0,
                },
                CpuStats {
                    cpu_index: 2,
                    total: 0,
                    idle: 0,
                },
            ],
            cpu_usages: &mut Vec::new(),
            ema_values: &mut vec![0.0; 3],
            high_utilization_cores: &mut Vec::new(),
            moderate_utilization_cores: &mut Vec::new(),
        };

        let threshold_high = 50.0;
        let threshold_moderate = 30.0;

        read_and_parse_cpu_stats(
            &mut context,
            threshold_high,
            threshold_moderate,
            file.path()
                .to_str()
                .ok_or_else(|| anyhow::anyhow!("Invalid file path"))?,
        )?;
        assert!(context.high_utilization_cores.len() >= 2);
        assert!(!context.moderate_utilization_cores.is_empty());

        Ok(())
    }

    #[test]
    fn test_read_and_parse_cpu_stats_resize_within_same_context() -> Result<(), anyhow::Error> {
        let one_cpu_file = NamedTempFile::new()?;
        writeln!(
            &one_cpu_file,
            "cpu  100 200 300 400 500 600 700 800 900 1000\n\
            cpu0 10 20 30 40 50 60 70 80 90 100"
        )?;

        let mut context = CpuStatsContext {
            prev_cpu_stats: &mut Vec::new(),
            cpu_usages: &mut Vec::new(),
            ema_values: &mut Vec::new(),
            high_utilization_cores: &mut Vec::new(),
            moderate_utilization_cores: &mut Vec::new(),
        };

        let threshold_high = 50.0;
        let threshold_moderate = 30.0;

        // Call read_and_parse_cpu_stats with file containing only one CPU
        read_and_parse_cpu_stats(
            &mut context,
            threshold_high,
            threshold_moderate,
            one_cpu_file
                .path()
                .to_str()
                .ok_or_else(|| anyhow::anyhow!("Invalid file path"))?,
        )?;

        assert_eq!(context.prev_cpu_stats.len(), 1);
        assert_eq!(context.cpu_usages.len(), 1);
        assert_eq!(context.ema_values.len(), 1);

        let cpu_stats_file = NamedTempFile::new()?;
        writeln!(
            &cpu_stats_file,
            "cpu  100 200 300 400 500 600 700 800 900 1000\n\
            cpu0 10 20 30 40 50 60 70 80 90 100\n\
            cpu1 90 30 40 50 60 70 80 90 100 110\n\
            cpu2 30 40 50 60 70 80 90 100 110 120\n\
            cpu3 50 60 70 80 90 100 110 120 130 140\n\
            cpu4 30 40 50 60 70 80 90 100 110 120\n\
            cpu5 30 40 50 60 70 80 90 100 110 120"
        )?;

        // Call read_and_parse_cpu_stats with file containing six CPUs
        read_and_parse_cpu_stats(
            &mut context,
            threshold_high,
            threshold_moderate,
            cpu_stats_file
                .path()
                .to_str()
                .ok_or_else(|| anyhow::anyhow!("Invalid file path"))?,
        )?;

        assert_eq!(context.prev_cpu_stats.len(), 6);
        assert_eq!(context.cpu_usages.len(), 6);
        assert_eq!(context.ema_values.len(), 6);

        Ok(())
    }

    #[test]
    fn test_calculate_epp_duration() {
        // Case1: Cores have moderate utilization
        let cpu_usages = vec![35.0, 38.0, 40.0];
        let threshold_moderate = 30.0;
        let mut all_below_threshold_moderate_start = Some(Instant::now());

        let args = EppCalculationArgs {
            cpu_usages: &cpu_usages,
            all_below_threshold_moderate_start: &mut all_below_threshold_moderate_start,
            high_utilization_cores: &[],
            moderate_utilization_cores: &[0, 1, 2],
            gpu_util_high: false,
            rtc_fs_value: false,
            bsm_state: false,
            media_cgroup_state: false,
            threshold_moderate,
        };

        let (new_epp, epp_duration) = calculate_epp_duration(args);
        let expected_duration = TimeConstantsIntel::cpu_sampling_time();

        // non MTL auto epp values being asserted
        // as MTL CPUID is not passed to this function
        assert_eq!(new_epp, Some(Epp::epp_bal()));
        assert_eq!(epp_duration, expected_duration);

        // Case2: Cores have high utilization
        let cpu_usages = vec![65.0, 15.0, 10.0];
        let threshold_moderate = 30.0;
        let mut all_below_threshold_moderate_start = Some(Instant::now());

        let args = EppCalculationArgs {
            cpu_usages: &cpu_usages,
            all_below_threshold_moderate_start: &mut all_below_threshold_moderate_start,
            high_utilization_cores: &[0],
            moderate_utilization_cores: &[],
            gpu_util_high: false,
            rtc_fs_value: false,
            bsm_state: false,
            media_cgroup_state: false,
            threshold_moderate,
        };

        let (new_epp, epp_duration) = calculate_epp_duration(args);
        let expected_duration = TimeConstantsIntel::high_threshold_time();

        // non MTL auto epp values being asserted
        assert_eq!(new_epp, Some(Epp::epp_perf()));
        assert_eq!(epp_duration, expected_duration);

        // Case3: GPU utilization is high
        let cpu_usages = vec![10.0, 20.0, 5.0];
        let threshold_moderate = 0.5;
        let mut all_below_threshold_moderate_start = Some(Instant::now());

        let args = EppCalculationArgs {
            cpu_usages: &cpu_usages,
            all_below_threshold_moderate_start: &mut all_below_threshold_moderate_start,
            high_utilization_cores: &[],
            moderate_utilization_cores: &[],
            gpu_util_high: true,
            rtc_fs_value: false,
            bsm_state: false,
            media_cgroup_state: false,
            threshold_moderate,
        };

        let (new_epp, epp_duration) = calculate_epp_duration(args);
        let expected_duration = TimeConstantsIntel::cpu_sampling_time();

        // non MTL auto epp values being asserted
        assert_eq!(new_epp, Some(Epp::epp_bal()));
        assert_eq!(epp_duration, expected_duration);
    }
}
