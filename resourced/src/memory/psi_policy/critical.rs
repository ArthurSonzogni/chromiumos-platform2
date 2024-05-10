// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::CalculationArgs;
use super::Config;
use super::MemoryReclaim;
use super::MemoryReclaimReason;
use super::PsiLevel;
use super::ReclaimCalculator;

const INITIAL_MULTIPLIER: usize = 1;
const MULTIPLIER_FACTOR: usize = 2;

pub struct CriticalCondition {
    /// If the system has been unresponsive for a long time, request memory reclaim more
    /// aggressively by increasing the memory reclaim target size exponetially.
    reclaim_target_multiplier: usize,
}

impl CriticalCondition {
    pub fn new() -> Self {
        Self {
            reclaim_target_multiplier: INITIAL_MULTIPLIER,
        }
    }
}

impl ReclaimCalculator for CriticalCondition {
    fn calculate(
        &mut self,
        config: &Config,
        args: &CalculationArgs,
    ) -> Option<(MemoryReclaim, MemoryReclaimReason)> {
        if args.psi_level < PsiLevel::Critical {
            return None;
        }
        // If it monitors multiple critical memory pressure in a row, it means that critical memory
        // pressure continues in the short time range. Increase the memory reclaim target size
        // exponetially in that case.
        if args
            .psi_level_log
            .iter()
            .all(|level| level >= &PsiLevel::Critical)
        {
            self.reclaim_target_multiplier = self
                .reclaim_target_multiplier
                .saturating_mul(MULTIPLIER_FACTOR);
        } else {
            self.reclaim_target_multiplier = INITIAL_MULTIPLIER;
        }

        let reclaim_target_kb = self
            .reclaim_target_multiplier
            .saturating_mul(config.unresponsive_reclaim_target_kb);
        Some((
            MemoryReclaim::Critical(reclaim_target_kb),
            MemoryReclaimReason::CriticalPressure,
        ))
    }
}
