// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::future;
use std::io;
use std::time::Duration;
use std::time::Instant;

use tokio;

use crate::feature;
use crate::memory::PSI_MEMORY_POLICY_FEATURE_NAME;
use crate::psi::PsiWatcher;
use crate::psi::Target;

const DOWNGRADE_DURATION: Duration = Duration::from_secs(2);
const MONITOR_WINDOW: Duration = Duration::from_secs(1);

const FEATURE_PARAM_BACKGROUND_PSI_SOME_MILLIS: &str = "BackgroundPsiSomeMillis";
const FEATURE_PARAM_FOREGROUND_PSI_FULL_MILLIS: &str = "ForegroundPsiFullMillis";
const FEATURE_PARAM_UNRESPONSIVE_PSI_FULL_MILLIS: &str = "UnresponsivePsiFullMillis";
const DEFAULT_BACKGROUND_PSI_SOME_MILLIS: u64 = 200;
const DEFAULT_FOREGROUND_PSI_FULL_MILLIS: u64 = 50;
const DEFAULT_UNRESPONSIVE_PSI_FULL_MILLIS: u64 = 500;

/// The current memory pressure level.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PsiLevel {
    /// There is no memory pressure.
    None,
    /// There is moderate memory pressure. Some task is getting slowed by swap-in or direct reclaim.
    Background,
    /// The system is getting slow by memory stall.
    Foreground,
    /// The system is getting stalled by memory.
    Critical,
}

#[derive(Debug, Clone)]
pub struct Config {
    background_psi_some_millis: u64,
    foreground_psi_full_millis: u64,
    unresponsive_psi_full_millis: u64,
}

impl Config {
    pub fn load_from_feature() -> anyhow::Result<Self> {
        Ok(Self {
            background_psi_some_millis: feature::get_feature_param_as(
                PSI_MEMORY_POLICY_FEATURE_NAME,
                FEATURE_PARAM_BACKGROUND_PSI_SOME_MILLIS,
            )?
            .unwrap_or(DEFAULT_BACKGROUND_PSI_SOME_MILLIS),
            foreground_psi_full_millis: feature::get_feature_param_as(
                PSI_MEMORY_POLICY_FEATURE_NAME,
                FEATURE_PARAM_FOREGROUND_PSI_FULL_MILLIS,
            )?
            .unwrap_or(DEFAULT_FOREGROUND_PSI_FULL_MILLIS),
            unresponsive_psi_full_millis: feature::get_feature_param_as(
                PSI_MEMORY_POLICY_FEATURE_NAME,
                FEATURE_PARAM_UNRESPONSIVE_PSI_FULL_MILLIS,
            )?
            .unwrap_or(DEFAULT_UNRESPONSIVE_PSI_FULL_MILLIS),
        })
    }
}

impl Default for Config {
    fn default() -> Self {
        Self {
            background_psi_some_millis: DEFAULT_BACKGROUND_PSI_SOME_MILLIS,
            foreground_psi_full_millis: DEFAULT_FOREGROUND_PSI_FULL_MILLIS,
            unresponsive_psi_full_millis: DEFAULT_UNRESPONSIVE_PSI_FULL_MILLIS,
        }
    }
}

/// Monitors the memory pressure of the system.
pub struct PsiMemoryPressureMonitor {
    background_psi_watcher: PsiWatcher,
    foreground_psi_watcher: PsiWatcher,
    unresponsive_psi_watcher: PsiWatcher,
    current_level: PsiLevel,
    /// The last time when PSI watcher is triggered.
    ///
    /// According to PSI documentation:
    /// https://docs.kernel.org/accounting/psi.html#monitoring-for-pressure-thresholds
    ///
    /// > When activated, psi monitor stays active for at least the duration of one tracking window
    /// > to avoid repeated activations/deactivations when system is bouncing in and out of the
    /// > stall state.
    ///
    /// The monitor waits to check PSI until it passes the window duration after the last
    /// activation.
    last_activated_at: Instant,
}

impl PsiMemoryPressureMonitor {
    /// Creates a new monitor.
    pub fn new(config: Config) -> io::Result<Self> {
        Self::new_with_initial_level(config, PsiLevel::None)
    }

    /// Creates a new monitor with an initial level.
    pub fn new_with_initial_level(config: Config, level: PsiLevel) -> io::Result<Self> {
        let background_psi_watcher = PsiWatcher::new_memory_pressure(
            Target::Some,
            Duration::from_millis(config.background_psi_some_millis),
            MONITOR_WINDOW,
        )?;
        let foreground_psi_watcher = PsiWatcher::new_memory_pressure(
            Target::Full,
            Duration::from_millis(config.foreground_psi_full_millis),
            MONITOR_WINDOW,
        )?;
        let unresponsive_psi_watcher = PsiWatcher::new_memory_pressure(
            Target::Full,
            Duration::from_millis(config.unresponsive_psi_full_millis),
            MONITOR_WINDOW,
        )?;
        Ok(Self {
            background_psi_watcher,
            foreground_psi_watcher,
            unresponsive_psi_watcher,
            current_level: level,
            last_activated_at: Instant::now() - MONITOR_WINDOW,
        })
    }

    /// Returns the current memory pressure level.
    pub fn current_level(&self) -> PsiLevel {
        self.current_level
    }

    /// Monitors the memory pressure of the system.
    pub async fn monitor(&mut self) -> io::Result<PsiLevel> {
        // When PSI is activated, it stays active for at least the duration of one tracking window.
        // The monitor waits for cool_down_duration to avoid getting duplicated events. See the
        // comment of PsiMemoryPressureMonitor::last_activated_at for details.
        let cool_down_duration = self.last_activated_at.elapsed();
        let background_waiter = async {
            if self.current_level > PsiLevel::Background {
                future::pending().await
            } else {
                if cool_down_duration < MONITOR_WINDOW {
                    tokio::time::sleep(MONITOR_WINDOW - cool_down_duration).await;
                }
                self.background_psi_watcher.wait().await
            }
        };
        let foreground_waiter = async {
            if self.current_level > PsiLevel::Foreground {
                future::pending().await
            } else {
                if cool_down_duration < MONITOR_WINDOW && self.current_level >= PsiLevel::Foreground
                {
                    tokio::time::sleep(MONITOR_WINDOW - cool_down_duration).await;
                }
                self.foreground_psi_watcher.wait().await
            }
        };
        let critical_waiter = async {
            if cool_down_duration < MONITOR_WINDOW && self.current_level == PsiLevel::Critical {
                tokio::time::sleep(MONITOR_WINDOW - cool_down_duration).await;
            }
            self.unresponsive_psi_watcher.wait().await
        };
        // The kernel only generates PSI events when the PSI rises above a configured threshold, so
        // there's no way to directly wait for PSI to drop below the moderate/critical threshold.
        // Instead, The downgrade timer indirectly waits for a period where there are no
        // moderate/critical signals, at which point we downgrade the pressure.
        // Note that we want these downgrade duration to be larger than the monitoring window to try
        // to avoid oscillating between pressure states.
        let downgrade_timer = async {
            if self.current_level == PsiLevel::None {
                future::pending().await
            } else {
                tokio::time::sleep(DOWNGRADE_DURATION).await
            }
        };

        self.current_level = tokio::select! {
            biased;
            result = critical_waiter => {
                result?;
                self.last_activated_at = Instant::now();
                PsiLevel::Critical
            },
            result = foreground_waiter => {
                result?;
                self.last_activated_at = Instant::now();
                PsiLevel::Foreground
            },
            result = background_waiter => {
                result?;
                self.last_activated_at = Instant::now();
                PsiLevel::Background
            },
            _ = downgrade_timer => {
                match self.current_level {
                    PsiLevel::Critical | PsiLevel::Foreground => {
                        PsiLevel::Background
                    },
                    _ => PsiLevel::None,
                }
            },
        };

        Ok(self.current_level)
    }
}
