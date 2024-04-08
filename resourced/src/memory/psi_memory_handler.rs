// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::future;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use std::time::Instant;

use anyhow::Context;
use log::error;
use log::info;
use tokio::sync::Notify;

use crate::common;
use crate::cpu_utils::Cpuset;
use crate::feature;
use crate::memory::get_chrome_memory_kb;
use crate::memory::get_component_margins_kb;
use crate::memory::meminfo::MemInfo;
use crate::memory::psi_monitor::Config as PsiMonitorConfig;
use crate::memory::psi_monitor::PsiLevel;
use crate::memory::psi_monitor::PsiMemoryPressureMonitor;
use crate::memory::psi_policy::Config as PsiPolicyConfig;
use crate::memory::psi_policy::MemoryReclaim;
use crate::memory::psi_policy::PsiMemoryPolicy;
use crate::memory::try_discard_stale_at_moderate;
use crate::memory::try_vmms_reclaim_memory;
use crate::memory::vmstat::Vmstat;
use crate::memory::ChromeProcessType;
use crate::memory::ComponentMarginsKb;
use crate::memory::PressureLevelArcContainer;
use crate::memory::PressureLevelArcvm;
use crate::memory::PressureLevelChrome;
use crate::memory::PressureStatus;
use crate::memory::DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME;
use crate::vm_memory_management_client::VmMemoryManagementClient;

const PSI_MEMORY_PRESSURE_DOWNGRADE_INTERVAL: Duration = Duration::from_secs(5);

/// Distributes the [MemoryReclaim] to VMs and host using [VmMemoryManagementClient]. This returns
/// [MemoryReclaim] for the host after distributing.
async fn distribute_memory_reclaim(
    vmms_client: &VmMemoryManagementClient,
    game_mode: common::GameMode,
    margins: &ComponentMarginsKb,
    reclaim: MemoryReclaim,
) -> PressureStatus {
    let discard_stale_at_moderate =
        feature::is_feature_enabled(DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME)
            .unwrap_or(false);

    let mut pressure_status = if vmms_client.is_active() {
        let (mut chrome_level, reclaim_target_kb) = reclaim.into();
        let background_memory_kb =
            get_chrome_memory_kb(ChromeProcessType::Background, None, reclaim_target_kb);
        let balloon_reclaim_kb = try_vmms_reclaim_memory(
            vmms_client,
            chrome_level,
            reclaim_target_kb,
            background_memory_kb,
            game_mode,
            discard_stale_at_moderate,
        )
        .await;
        let chrome_reclaim_target_kb = reclaim_target_kb.saturating_sub(balloon_reclaim_kb);
        if chrome_reclaim_target_kb == 0 {
            // Downgrade pressure level.
            chrome_level = match chrome_level {
                PressureLevelChrome::Critical => PressureLevelChrome::Moderate,
                PressureLevelChrome::Moderate => PressureLevelChrome::None,
                PressureLevelChrome::None => PressureLevelChrome::None,
            }
        }

        PressureStatus {
            chrome_level,
            chrome_reclaim_target_kb,
            arcvm_level: PressureLevelArcvm::None,
            arcvm_reclaim_target_kb: 0,
            arc_container_level: PressureLevelArcContainer::None,
            arc_container_reclaim_target_kb: 0,
        }
    } else {
        let (chrome_level, arcvm_level, arc_container_level, reclaim_target_kb) = match reclaim {
            MemoryReclaim::Critical(target_kb) => (
                PressureLevelChrome::Critical,
                if game_mode == common::GameMode::Arc {
                    PressureLevelArcvm::Cached
                } else {
                    PressureLevelArcvm::Perceptible
                },
                PressureLevelArcContainer::Perceptible,
                target_kb.try_into().unwrap_or(u64::MAX),
            ),
            MemoryReclaim::Moderate(target_kb) => (
                PressureLevelChrome::Moderate,
                PressureLevelArcvm::Cached,
                PressureLevelArcContainer::Cached,
                target_kb.try_into().unwrap_or(u64::MAX),
            ),
            MemoryReclaim::None => (
                PressureLevelChrome::None,
                PressureLevelArcvm::None,
                PressureLevelArcContainer::None,
                0,
            ),
        };

        // It is impossible to split the reclaim between host and ARC because resourced does not
        // know how much memory of ARC are available to reclaim without VMMMS. Use the same reclaim
        // target for both host and ARC pessimistically for the case ARC has no memory to reclaim.
        PressureStatus {
            chrome_level,
            chrome_reclaim_target_kb: reclaim_target_kb,
            arcvm_level,
            arcvm_reclaim_target_kb: reclaim_target_kb,
            arc_container_level,
            arc_container_reclaim_target_kb: reclaim_target_kb,
        }
    };

    if discard_stale_at_moderate {
        (
            pressure_status.chrome_level,
            pressure_status.chrome_reclaim_target_kb,
        ) = try_discard_stale_at_moderate(
            margins,
            pressure_status.chrome_level,
            pressure_status.chrome_reclaim_target_kb,
        );
    }

    pressure_status
}

pub struct PsiMemoryHandler {
    psi_monitor: PsiMemoryPressureMonitor,
    psi_memory_policy: PsiMemoryPolicy,
}

impl PsiMemoryHandler {
    pub fn new(root: &Path) -> anyhow::Result<Self> {
        let n_cpu = Cpuset::all_cores(root).context("load root cpuset")?.len();
        let monitor_config = match PsiMonitorConfig::load_from_feature() {
            Ok(config) => config,
            Err(e) => {
                error!("Failed to load PsiMonitorConfig: {}", e);
                PsiMonitorConfig::default()
            }
        };
        let policy_config = match PsiPolicyConfig::load_from_feature() {
            Ok(config) => config,
            Err(e) => {
                error!("Failed to load PsiPolicyConfig: {}", e);
                PsiPolicyConfig::default()
            }
        };
        let psi_monitor =
            PsiMemoryPressureMonitor::new(monitor_config).context("create psi monitor")?;
        let now = Instant::now();
        let vmstat = Vmstat::load().context("load vmstat")?;
        let psi_memory_policy = PsiMemoryPolicy::new(policy_config, n_cpu, now, vmstat);
        Ok(Self {
            psi_monitor,
            psi_memory_policy,
        })
    }

    pub fn reload_feature_params(&mut self) {
        match PsiMonitorConfig::load_from_feature() {
            Ok(config) => {
                let current_level = self.psi_monitor.current_level();
                match PsiMemoryPressureMonitor::new_with_initial_level(config, current_level) {
                    Ok(monitor) => self.psi_monitor = monitor,
                    Err(e) => error!("Failed to reload PsiMemoryPressureMonitor: {}", e),
                };
            }
            Err(e) => error!("Failed to reload PsiMonitorConfig: {}", e),
        }
        match PsiPolicyConfig::load_from_feature() {
            Ok(config) => self.psi_memory_policy.update_config(config),
            Err(e) => error!("Failed to reload PsiPolicyConfig: {}", e),
        }
    }

    /// Waits for memory pressure signal from PSI and calculate [PressureStatus].
    ///
    /// Returns [PressureStatus] if memory pressure is detected.
    ///
    /// Returns [PressureStatus::None] if feature flags are changed.
    ///
    /// If vmms_client is active, this method requests vmms to reclaim memory internally and adjust
    /// the reclaim target accordingly.
    pub async fn monitor_memory_pressure(
        &mut self,
        vmms_client: &VmMemoryManagementClient,
        feature_notify: &Arc<Notify>,
    ) -> anyhow::Result<Option<PressureStatus>> {
        let current_psi_level = self.psi_monitor.current_level();
        let psi_level = tokio::select! {
            result = self.psi_monitor.monitor() =>
                result.context("wait psi monitor")?,
            _ = async {
                if self.psi_memory_policy.is_low_memory() && current_psi_level == PsiLevel::None {
                    // If psi_monitor reports PsiLevel::None but psi_memory_policy still detects
                    // memory pressure, then we need to generate periodic pressure signals until
                    // memory pressure is resolved.
                    tokio::time::sleep(PSI_MEMORY_PRESSURE_DOWNGRADE_INTERVAL).await;
                } else {
                    future::pending::<()>().await;
                }
            } =>
                current_psi_level,
            _ = feature_notify.notified() =>
                return Ok(None),
        };
        let status = self
            .reclaim_on_memory_pressure(vmms_client, psi_level)
            .await?;
        Ok(Some(status))
    }

    async fn reclaim_on_memory_pressure(
        &mut self,
        vmms_client: &VmMemoryManagementClient,
        psi_level: PsiLevel,
    ) -> anyhow::Result<PressureStatus> {
        let game_mode = common::get_game_mode();
        let margins = get_component_margins_kb();
        let now = Instant::now();
        let meminfo = MemInfo::load().context("load meminfo")?;
        let vmstat = Vmstat::load().context("load vmstat")?;
        let (reclaim, reason) = self
            .psi_memory_policy
            .calculate_reclaim(now, &meminfo, vmstat, game_mode, &margins, psi_level);

        if let MemoryReclaim::Critical(target_kb) = &reclaim {
            info!("PSI Memory Reclaim: {} KB, reason: {:?}", target_kb, reason);
            // TODO(kawasin): log the reason to UMA
        }

        let pressure_status =
            distribute_memory_reclaim(vmms_client, game_mode, &margins, reclaim).await;

        Ok(pressure_status)
    }
}
