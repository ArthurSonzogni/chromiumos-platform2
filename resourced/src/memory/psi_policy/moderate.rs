// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::CalculationArgs;
use super::Config;
use super::MemoryReclaim;
use super::MemoryReclaimReason;
use super::PsiLevel;
use super::ReclaimCalculator;

pub struct ModerateCondition {}

impl ModerateCondition {
    pub fn new() -> Self {
        Self {}
    }
}

impl ReclaimCalculator for ModerateCondition {
    fn calculate(
        &mut self,
        config: &Config,
        args: &CalculationArgs,
    ) -> Option<(MemoryReclaim, MemoryReclaimReason)> {
        if args.psi_level > PsiLevel::None {
            Some((
                MemoryReclaim::Moderate(config.moderate_reclaim_target_kb),
                MemoryReclaimReason::Unknown,
            ))
        } else {
            None
        }
    }
}
