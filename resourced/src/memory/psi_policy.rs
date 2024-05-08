// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::Ordering;
use std::time::Duration;
use std::time::Instant;

use crate::common::GameMode;
use crate::feature;
use crate::memory::get_background_available_memory_kb;
use crate::memory::meminfo::MemInfo;
use crate::memory::page_size::get_page_size;
use crate::memory::psi_monitor::PsiLevel;
use crate::memory::vmstat::Vmstat;
use crate::memory::ComponentMarginsKb;
use crate::memory::PressureLevelChrome;
use crate::memory::PSI_MEMORY_POLICY_FEATURE_NAME;

const ONE_KB: usize = 1024;
const PSI_LEVEL_LOG_WINDOW_SIZE: usize = 3;
const MINIMUM_VMSTAT_DURATION: Duration = Duration::from_millis(100);

const FEATURE_PARAM_UNRESPONSIVE_RECLAIM_TARGET_KB: &str = "UnresponsiveReclaimTargetKb";
const FEATURE_PARAM_SLOW_RECLAIM_TARGET_KB: &str = "SlowReclaimTargetKb";
const FEATURE_PARAM_MODERATE_RECLAIM_TARGET_KB: &str = "ModerateReclaimTargetKb";
const FEATURE_PARAM_THRASHING_ANON_THRESHOLD_KB: &str = "ThrashingAnonThresholdKb";
const FEATURE_PARAM_THRASHING_FILE_THRESHOLD_KB: &str = "ThrashingFileThresholdKb";
const FEATURE_PARAM_PGSTEAL_DIRECT_THRESHOLD_KB: &str = "PgstealDirectThresholdKb";
const DEFAULT_UNRESPONSIVE_RECLAIM_TARGET_KB: usize = 100 * 1024;
const DEFAULT_SLOW_RECLAIM_TARGET_KB: usize = 100 * 1024;
const DEFAULT_MODERATE_RECLAIM_TARGET_KB: usize = 10 * 1024;
const DEFAULT_THRASHING_ANON_THRESHOLD_KB: usize = 10 * 1024;
const DEFAULT_THRASHING_FILE_THRESHOLD_KB: usize = 10 * 1024;
const DEFAULT_PGSTEAL_DIRECT_THRESHOLD_KB: usize = 10 * 1024;

/// [MemoryReclaim] describes how much memory to reclaim on memory pressure and its severity.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum MemoryReclaim {
    None,
    Moderate(usize),
    Critical(usize),
}

impl From<(PressureLevelChrome, u64)> for MemoryReclaim {
    fn from((level, size): (PressureLevelChrome, u64)) -> Self {
        let size = size.try_into().unwrap_or(usize::MAX);
        match level {
            PressureLevelChrome::None => MemoryReclaim::None,
            PressureLevelChrome::Moderate => MemoryReclaim::Moderate(size),
            PressureLevelChrome::Critical => MemoryReclaim::Critical(size),
        }
    }
}

impl From<MemoryReclaim> for (PressureLevelChrome, u64) {
    fn from(reclaim: MemoryReclaim) -> Self {
        match reclaim {
            MemoryReclaim::None => (PressureLevelChrome::None, 0),
            MemoryReclaim::Moderate(size) => (
                PressureLevelChrome::Moderate,
                size.try_into().unwrap_or(u64::MAX),
            ),
            MemoryReclaim::Critical(size) => (
                PressureLevelChrome::Critical,
                size.try_into().unwrap_or(u64::MAX),
            ),
        }
    }
}

impl PartialOrd for MemoryReclaim {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        match (self, other) {
            (MemoryReclaim::Critical(v1), MemoryReclaim::Critical(v2)) => Some(v1.cmp(v2)),
            (MemoryReclaim::Critical(_), _) => Some(Ordering::Greater),
            (_, MemoryReclaim::Critical(_)) => Some(Ordering::Less),
            (MemoryReclaim::Moderate(v1), MemoryReclaim::Moderate(v2)) => Some(v1.cmp(v2)),
            (MemoryReclaim::Moderate(_), _) => Some(Ordering::Greater),
            (_, MemoryReclaim::Moderate(_)) => Some(Ordering::Less),
            (MemoryReclaim::None, MemoryReclaim::None) => Some(std::cmp::Ordering::Equal),
        }
    }
}

/// The reason of [MemoryReclaim].
///
/// The representing numbers are directly sent to UMA. Do not reuse the number in the future when
/// you add a new reason. Also You need to update [MAX_MEMORY_RECLAIM_REASON] when you add a new
/// reason.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemoryReclaimReason {
    Unknown = 0,
    CriticalPressure = 1,
    SystemSlow = 2,
    ThrashingAnon = 3,
    ThrashingFile = 4,
    DirectReclaim = 5,
    Margin = 6,
}

pub const MAX_MEMORY_RECLAIM_REASON: i32 = MemoryReclaimReason::Margin as i32;

fn calculate_per_sec_kb(after_pages: usize, before_pages: usize, duration: Duration) -> usize {
    let total_kb = (after_pages - before_pages) * get_page_size() / ONE_KB;
    total_kb * 1000 / duration.as_millis() as usize
}

#[derive(Debug, Clone)]
pub struct Config {
    unresponsive_reclaim_target_kb: usize,
    slow_reclaim_target_kb: usize,
    moderate_reclaim_target_kb: usize,
    thrashing_anon_threshold_kb: usize,
    thrashing_file_threshold_kb: usize,
    pgsteal_direct_threshold_kb: usize,
}

impl Config {
    pub fn load_from_feature() -> anyhow::Result<Self> {
        let mut config = Self::default();
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_UNRESPONSIVE_RECLAIM_TARGET_KB,
        )? {
            config.unresponsive_reclaim_target_kb = value;
        }
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_SLOW_RECLAIM_TARGET_KB,
        )? {
            config.slow_reclaim_target_kb = value;
        }
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_MODERATE_RECLAIM_TARGET_KB,
        )? {
            config.moderate_reclaim_target_kb = value;
        }
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_THRASHING_ANON_THRESHOLD_KB,
        )? {
            config.thrashing_anon_threshold_kb = value;
        }
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_THRASHING_FILE_THRESHOLD_KB,
        )? {
            config.thrashing_file_threshold_kb = value;
        }
        if let Some(value) = feature::get_feature_param_as(
            PSI_MEMORY_POLICY_FEATURE_NAME,
            FEATURE_PARAM_PGSTEAL_DIRECT_THRESHOLD_KB,
        )? {
            config.pgsteal_direct_threshold_kb = value;
        }
        Ok(config)
    }
}

impl Default for Config {
    fn default() -> Self {
        Self {
            unresponsive_reclaim_target_kb: DEFAULT_UNRESPONSIVE_RECLAIM_TARGET_KB,
            slow_reclaim_target_kb: DEFAULT_SLOW_RECLAIM_TARGET_KB,
            moderate_reclaim_target_kb: DEFAULT_MODERATE_RECLAIM_TARGET_KB,
            thrashing_anon_threshold_kb: DEFAULT_THRASHING_ANON_THRESHOLD_KB,
            thrashing_file_threshold_kb: DEFAULT_THRASHING_FILE_THRESHOLD_KB,
            pgsteal_direct_threshold_kb: DEFAULT_PGSTEAL_DIRECT_THRESHOLD_KB,
        }
    }
}

/// A policy decides how much memory to reclaim on memory pressure.
#[derive(Debug)]
pub struct PsiMemoryPolicy {
    config: Config,
    last_reclaim: MemoryReclaim,
    /// The last vmstat metrics.
    ///
    /// PsiMemoryPolicy uses the differences of vmstat metrics to calculate the memory reclaim.
    /// (e.g. workingset_refault_anon).
    last_vmstat: Vmstat,
    /// The time when the last vmstat was loaded.
    last_vmstat_at: Instant,
    /// If the system has been unresponsive for a long time, request memory reclaim more
    /// aggressively by increasing the memory reclaim target size exponetially.
    reclaim_target_multiplier: usize,
    /// The ring buffer storing last several psi levels.
    ///
    /// This is used to increase booster.
    psi_level_log: [PsiLevel; PSI_LEVEL_LOG_WINDOW_SIZE],
    /// The head index of the ring-buffered psi level log.
    idx_psi_level_log: usize,
    /// Thresholds to detect thrashing varies per device.
    thrashing_threshold_multiplier: usize,
}

impl PsiMemoryPolicy {
    /// Create a new [PSIMemoryPolicy].
    pub fn new(config: Config, n_cpu: usize, now: Instant, vmstat: Vmstat) -> Self {
        Self {
            config,
            last_reclaim: MemoryReclaim::None,
            last_vmstat_at: now,
            last_vmstat: vmstat,
            idx_psi_level_log: 0,
            psi_level_log: [PsiLevel::None; PSI_LEVEL_LOG_WINDOW_SIZE],
            reclaim_target_multiplier: 1,
            // The impact of thrashing on performance varies depending on the number of CPU cores.
            thrashing_threshold_multiplier: n_cpu,
        }
    }

    /// Calculate the memory reclaim based on the current memory pressure.
    pub fn calculate_reclaim(
        &mut self,
        now: Instant,
        current_meminfo: &MemInfo,
        current_vmstat: Vmstat,
        game_mode: GameMode,
        margins: &ComponentMarginsKb,
        psi_level: PsiLevel,
    ) -> (MemoryReclaim, MemoryReclaimReason) {
        let mut reason = MemoryReclaimReason::Unknown;
        let vmstat_duration = now.duration_since(self.last_vmstat_at);

        let mut reclaim = match psi_level {
            PsiLevel::Critical => {
                // If it monitors multiple critical memory pressure in a row, it means that critical
                // memory pressure continues in the short time range. Increase the memory reclaim
                // target size exponetially in that case.
                if self
                    .psi_level_log
                    .iter()
                    .all(|level| level == &PsiLevel::Critical)
                {
                    self.reclaim_target_multiplier =
                        self.reclaim_target_multiplier.saturating_mul(2);
                } else {
                    self.reclaim_target_multiplier = 1;
                }

                let reclaim_target_kb = self
                    .reclaim_target_multiplier
                    .saturating_mul(self.config.unresponsive_reclaim_target_kb);
                reason = MemoryReclaimReason::CriticalPressure;
                MemoryReclaim::Critical(reclaim_target_kb)
            }
            PsiLevel::Foreground => {
                reason = MemoryReclaimReason::SystemSlow;
                MemoryReclaim::Critical(self.config.slow_reclaim_target_kb)
            }
            // The monitor can send signal in less than the monitoring window 1s if it gets a
            // moderate PSI event just after downgrade timer fires. If the duration is too short,
            // skip calculating vmstat metrics since we don't have enough confidence that the kb/s
            // values are going to be accurate. If the system is actually thrashing, we should get
            // another moderate PSI event soon.
            PsiLevel::Background if vmstat_duration < MINIMUM_VMSTAT_DURATION => {
                MemoryReclaim::Moderate(self.config.moderate_reclaim_target_kb)
            }
            PsiLevel::Background => {
                let thrashing_anon_per_sec_kb = calculate_per_sec_kb(
                    current_vmstat.workingset_refault_anon,
                    self.last_vmstat.workingset_refault_anon,
                    vmstat_duration,
                );
                let thrashing_file_per_sec_kb = calculate_per_sec_kb(
                    current_vmstat.workingset_refault_file,
                    self.last_vmstat.workingset_refault_file,
                    vmstat_duration,
                );
                let pgsteal_direct_per_sec_kb = calculate_per_sec_kb(
                    current_vmstat.pgsteal_direct,
                    self.last_vmstat.pgsteal_direct,
                    vmstat_duration,
                );
                if thrashing_anon_per_sec_kb
                    >= self.config.thrashing_anon_threshold_kb * self.thrashing_threshold_multiplier
                {
                    reason = MemoryReclaimReason::ThrashingAnon;
                    MemoryReclaim::Critical(thrashing_anon_per_sec_kb)
                } else if thrashing_file_per_sec_kb
                    >= self.config.thrashing_file_threshold_kb * self.thrashing_threshold_multiplier
                {
                    reason = MemoryReclaimReason::ThrashingFile;
                    MemoryReclaim::Critical(thrashing_file_per_sec_kb)
                } else if pgsteal_direct_per_sec_kb
                    >= self.config.pgsteal_direct_threshold_kb * self.thrashing_threshold_multiplier
                {
                    reason = MemoryReclaimReason::DirectReclaim;
                    MemoryReclaim::Critical(pgsteal_direct_per_sec_kb)
                } else {
                    MemoryReclaim::Moderate(self.config.moderate_reclaim_target_kb)
                }
            }
            PsiLevel::None => MemoryReclaim::None,
        };

        // Calculate the memory reclaim based on the memory margin and use higher memory reclaim.
        let available = get_background_available_memory_kb(current_meminfo, game_mode);
        let reclaim_margin = margins.compute_chrome_pressure(available).into();
        if reclaim_margin > reclaim {
            reason = MemoryReclaimReason::Margin;
            reclaim = reclaim_margin;
        }

        match &mut reclaim {
            MemoryReclaim::Critical(reclaim_target_kb)
            | MemoryReclaim::Moderate(reclaim_target_kb) => {
                let swap_used_kb = current_meminfo
                    .swap_total
                    .saturating_sub(current_meminfo.swap_free)
                    as usize;
                // Reclaim target can be too big in edge cases (e.g. CriticalPressure increases the
                // size exponentially if there are critical pressure events in a row). Reclaim
                // target is capped because reclaiming the whole swap memory is too aggressive.
                *reclaim_target_kb = (*reclaim_target_kb).min(swap_used_kb / 2);
            }
            _ => {}
        }

        if now.duration_since(self.last_vmstat_at) > MINIMUM_VMSTAT_DURATION {
            self.last_vmstat = current_vmstat;
            self.last_vmstat_at = now;
        }
        self.last_reclaim = reclaim;
        self.psi_level_log[self.idx_psi_level_log] = psi_level;
        self.idx_psi_level_log = (self.idx_psi_level_log + 1) % PSI_LEVEL_LOG_WINDOW_SIZE;

        (reclaim, reason)
    }

    /// Memory reclaim mode in this policy is low memory mode until [MemoryReclaim::None].
    pub fn is_low_memory(&self) -> bool {
        !matches!(self.last_reclaim, MemoryReclaim::None)
    }

    /// Update the configuration.
    pub fn update_config(&mut self, config: Config) {
        self.config = config;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::memory::get_memory_parameters;
    use crate::memory::ArcMarginsKb;

    fn default_margin() -> ComponentMarginsKb {
        let critical = 10 * 1024;
        let moderate = 50 * 1024;
        ComponentMarginsKb {
            chrome_critical: critical,
            chrome_moderate: moderate,
            arcvm: ArcMarginsKb {
                foreground: critical * 3 / 4,
                perceptible: critical,
                cached: 2 * critical,
            },
            arc_container: ArcMarginsKb {
                foreground: 0,
                perceptible: critical,
                cached: std::cmp::min(moderate, 2 * critical),
            },
        }
    }

    fn default_meminfo() -> MemInfo {
        let mem_param = get_memory_parameters();
        MemInfo {
            total: 4 * 1024 * 1024,
            free: 1024 * 1024 + mem_param.reserved_free,
            swap_free: 0,
            swap_total: 12 * 1024 * 1024,
            ..MemInfo::default()
        }
    }

    #[test]
    fn test_memory_reclaim_order() {
        assert!(MemoryReclaim::Critical(1024) == MemoryReclaim::Critical(1024));
        assert!(MemoryReclaim::Critical(1024) > MemoryReclaim::Critical(10));
        assert!(MemoryReclaim::Critical(10) > MemoryReclaim::Moderate(1024));
        assert!(MemoryReclaim::Critical(10) > MemoryReclaim::None);
        assert!(MemoryReclaim::Moderate(1024) == MemoryReclaim::Moderate(1024));
        assert!(MemoryReclaim::Moderate(10) < MemoryReclaim::Moderate(1024));
        assert!(MemoryReclaim::Moderate(10) > MemoryReclaim::None);
        assert!(MemoryReclaim::None == MemoryReclaim::None);
    }

    #[test]
    fn test_create_reclaim_critical_pressure() {
        let config = Config::default();
        let now = Instant::now();
        let vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Critical;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat,
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::CriticalPressure);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.unresponsive_reclaim_target_kb)
        );
    }

    #[test]
    fn test_create_reclaim_moderate_pressure() {
        let config = Config::default();
        let now = Instant::now();
        let vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat,
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert_eq!(
            reclaim,
            MemoryReclaim::Moderate(config.moderate_reclaim_target_kb)
        );
    }

    #[test]
    fn test_create_reclaim_thrashing_anon() {
        let config = Config::default();
        let mut now = Instant::now();
        let mut vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        now += Duration::from_secs(1);
        vmstat.workingset_refault_anon +=
            config.thrashing_anon_threshold_kb * 1024 / get_page_size();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingAnon);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.thrashing_anon_threshold_kb)
        );

        now += Duration::from_millis(500);
        let delta_thrashing_anon_kb = config.thrashing_anon_threshold_kb + 4096;
        vmstat.workingset_refault_anon += delta_thrashing_anon_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingAnon);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(delta_thrashing_anon_kb * 2)
        );

        // n_cpu = 2
        let mut policy = PsiMemoryPolicy::new(config.clone(), 2, now, vmstat.clone());
        now += Duration::from_secs(1);
        let delta_thrashing_anon_kb = 2 * config.thrashing_anon_threshold_kb - 1;
        vmstat.workingset_refault_anon += delta_thrashing_anon_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert!(matches!(reclaim, MemoryReclaim::Moderate(_)));

        now += Duration::from_secs(1);
        let delta_thrashing_anon_kb = 2 * config.thrashing_anon_threshold_kb;
        vmstat.workingset_refault_anon += delta_thrashing_anon_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingAnon);
        assert_eq!(reclaim, MemoryReclaim::Critical(delta_thrashing_anon_kb));
    }

    #[test]
    fn test_create_reclaim_thrashing_file() {
        let config = Config::default();
        let mut now = Instant::now();
        let mut vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        now += Duration::from_secs(1);
        vmstat.workingset_refault_file +=
            config.thrashing_file_threshold_kb * 1024 / get_page_size();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingFile);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.thrashing_file_threshold_kb)
        );

        now += Duration::from_millis(500);
        let delta_thrashing_file_kb = config.thrashing_file_threshold_kb + 4096;
        vmstat.workingset_refault_file += delta_thrashing_file_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingFile);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(delta_thrashing_file_kb * 2)
        );

        // n_cpu = 2
        let mut policy = PsiMemoryPolicy::new(config.clone(), 2, now, vmstat.clone());
        now += Duration::from_secs(1);
        let delta_thrashing_file_kb = 2 * config.thrashing_file_threshold_kb - 1;
        vmstat.workingset_refault_file += delta_thrashing_file_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert!(matches!(reclaim, MemoryReclaim::Moderate(_)));

        now += Duration::from_secs(1);
        let delta_thrashing_file_kb = 2 * config.thrashing_file_threshold_kb;
        vmstat.workingset_refault_file += delta_thrashing_file_kb * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingFile);
        assert_eq!(reclaim, MemoryReclaim::Critical(delta_thrashing_file_kb));
    }

    #[test]
    fn test_create_reclaim_direct_reclaim() {
        let config = Config::default();
        let mut now = Instant::now();
        let mut vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        now += Duration::from_secs(1);
        vmstat.pgsteal_direct += config.pgsteal_direct_threshold_kb * 1024 / get_page_size();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::DirectReclaim);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.pgsteal_direct_threshold_kb)
        );

        now += Duration::from_millis(500);
        let pgsteal_direct = config.pgsteal_direct_threshold_kb + 4096;
        vmstat.pgsteal_direct += pgsteal_direct * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::DirectReclaim);
        assert_eq!(reclaim, MemoryReclaim::Critical(pgsteal_direct * 2));

        // n_cpu = 2
        let mut policy = PsiMemoryPolicy::new(config.clone(), 2, now, vmstat.clone());
        now += Duration::from_secs(1);
        let pgsteal_direct = 2 * config.pgsteal_direct_threshold_kb - 1;
        vmstat.pgsteal_direct += pgsteal_direct * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert!(matches!(reclaim, MemoryReclaim::Moderate(_)));

        now += Duration::from_secs(1);
        let pgsteal_direct = 2 * config.pgsteal_direct_threshold_kb;
        vmstat.pgsteal_direct += pgsteal_direct * 1024 / get_page_size();
        let (reclaim, reason) = policy.calculate_reclaim(
            now,
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::DirectReclaim);
        assert_eq!(reclaim, MemoryReclaim::Critical(pgsteal_direct));
    }

    #[test]
    fn test_create_reclaim_margin() {
        let config = Config::default();
        let now = Instant::now();
        let vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let mut current_meminfo = default_meminfo();
        current_meminfo.free = 0;
        let game_mode = GameMode::Off;
        let margins = default_margin();
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Margin);
        assert!(matches!(reclaim, MemoryReclaim::Critical(_)));
    }

    #[test]
    fn test_create_reclaim_critical_pressure_in_a_row() {
        let config = Config::default();
        let now = Instant::now();
        let vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let mut current_meminfo = default_meminfo();
        let game_mode = GameMode::Off;
        let margins = default_margin();
        for i in 1..4 {
            let (reclaim, reason) = policy.calculate_reclaim(
                now + Duration::from_secs(i),
                &current_meminfo,
                vmstat.clone(),
                game_mode,
                &margins,
                PsiLevel::Critical,
            );
            assert_eq!(reason, MemoryReclaimReason::CriticalPressure);
            assert_eq!(
                reclaim,
                MemoryReclaim::Critical(config.unresponsive_reclaim_target_kb)
            );
        }
        for i in 4..8 {
            let (reclaim, reason) = policy.calculate_reclaim(
                now + Duration::from_secs(i),
                &current_meminfo,
                vmstat.clone(),
                game_mode,
                &margins,
                PsiLevel::Critical,
            );
            assert_eq!(reason, MemoryReclaimReason::CriticalPressure);
            let target = config.unresponsive_reclaim_target_kb * (2_usize.pow(i as u32 - 3));
            assert_eq!(reclaim, MemoryReclaim::Critical(target));
        }

        current_meminfo.swap_free = current_meminfo.swap_total - 10 * 1024;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(9),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            PsiLevel::Critical,
        );
        // Capped by half of swap usage.
        assert_eq!(reason, MemoryReclaimReason::CriticalPressure);
        assert_eq!(reclaim, MemoryReclaim::Critical(5 * 1024));

        // reclaim target booster is reset when lower pressure is triggered.
        let _ = policy.calculate_reclaim(
            now + Duration::from_secs(10),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            PsiLevel::Background,
        );
        current_meminfo.swap_free = 0;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(12),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            PsiLevel::Critical,
        );
        assert_eq!(reason, MemoryReclaimReason::CriticalPressure);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.unresponsive_reclaim_target_kb)
        );
    }

    #[test]
    fn test_create_reclaim_in_a_short_period() {
        let config = Config::default();
        let now = Instant::now();
        let mut vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        let diff_pages = config.thrashing_anon_threshold_kb * 1024 / get_page_size();
        vmstat.workingset_refault_anon += diff_pages;
        let game_mode = GameMode::Off;
        let margins = default_margin();
        // If the duration is too short, skip vmstat kb/s calculation.
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_nanos(1),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            PsiLevel::Background,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert_eq!(
            reclaim,
            MemoryReclaim::Moderate(config.moderate_reclaim_target_kb)
        );

        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            PsiLevel::Background,
        );
        assert_eq!(reason, MemoryReclaimReason::ThrashingAnon);
        assert_eq!(
            reclaim,
            MemoryReclaim::Critical(config.thrashing_anon_threshold_kb)
        );
    }

    #[test]
    fn test_is_low_memory() {
        let config = Config::default();
        let now = Instant::now();
        let vmstat = Vmstat::default();
        let mut policy = PsiMemoryPolicy::new(config.clone(), 1, now, vmstat.clone());

        let current_meminfo = default_meminfo();
        let game_mode = GameMode::Off;
        let mut margins = default_margin();
        margins.chrome_critical = 0;
        margins.chrome_moderate = 0;
        let psi_level = PsiLevel::Background;
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat.clone(),
            game_mode,
            &margins,
            psi_level,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert!(matches!(reclaim, MemoryReclaim::Moderate(_)));
        assert!(policy.is_low_memory());

        let margins = default_margin();
        let current_meminfo = default_meminfo();
        let (reclaim, reason) = policy.calculate_reclaim(
            now + Duration::from_secs(1),
            &current_meminfo,
            vmstat,
            game_mode,
            &margins,
            PsiLevel::None,
        );
        assert_eq!(reason, MemoryReclaimReason::Unknown);
        assert_eq!(reclaim, MemoryReclaim::None);
        assert!(!policy.is_low_memory());
    }
}
