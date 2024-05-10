// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::CalculationArgs;
use super::Config;
use super::MemoryReclaim;
use super::MemoryReclaimReason;
use super::ReclaimCalculator;
use crate::memory::get_background_available_memory_kb;

pub struct MarginCondition {}

impl MarginCondition {
    pub fn new() -> Self {
        Self {}
    }
}

impl ReclaimCalculator for MarginCondition {
    fn calculate(
        &mut self,
        _config: &Config,
        args: &CalculationArgs,
    ) -> Option<(MemoryReclaim, MemoryReclaimReason)> {
        let available = get_background_available_memory_kb(args.meminfo, args.game_mode);
        let reclaim = args.margins.compute_chrome_pressure(available).into();
        if reclaim > MemoryReclaim::None {
            Some((reclaim, MemoryReclaimReason::Margin))
        } else {
            None
        }
    }
}
