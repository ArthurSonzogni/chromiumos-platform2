// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::CalculationArgs;
use super::Config;
use super::MemoryReclaim;
use super::MemoryReclaimReason;
use super::PsiLevel;
use super::ReclaimCalculator;

pub struct SystemSlowCondition {}

impl SystemSlowCondition {
    pub fn new() -> Self {
        Self {}
    }
}

impl ReclaimCalculator for SystemSlowCondition {
    fn calculate(
        &mut self,
        config: &Config,
        args: &CalculationArgs,
    ) -> Option<(MemoryReclaim, MemoryReclaimReason)> {
        if args.psi_level >= PsiLevel::Foreground {
            Some((
                MemoryReclaim::Critical(config.slow_reclaim_target_kb),
                MemoryReclaimReason::SystemSlow,
            ))
        } else {
            None
        }
    }
}
