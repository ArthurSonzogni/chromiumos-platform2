// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use super::CalculationArgs;
use super::Config;
use super::MemoryReclaim;
use super::MemoryReclaimReason;
use super::PsiLevel;
use super::ReclaimCalculator;
use super::MINIMUM_VMSTAT_DURATION;
use crate::memory::page_size::get_page_size;

const ONE_KB: usize = 1024;

fn calculate_per_sec_kb(
    after_pages: usize,
    before_pages: usize,
    duration: Duration,
) -> Option<usize> {
    if duration < MINIMUM_VMSTAT_DURATION {
        // The monitor can send signal in less than the monitoring window 1s if it gets a
        // moderate PSI event just after downgrade timer fires. If the duration is too short,
        // skip calculating vmstat metrics since we don't have enough confidence that the kb/s
        // values are going to be accurate. If the system is actually thrashing, we should get
        // another moderate PSI event soon.
        return None;
    }
    let total_kb = (after_pages - before_pages) * get_page_size() / ONE_KB;
    Some(total_kb * 1000 / duration.as_millis() as usize)
}

macro_rules! thrashing_reclaim_calculator {
    ($calculator_name:ident, $vmstat_property:ident, $threshold_property:ident, $reason:expr) => {
        pub struct $calculator_name {}

        impl $calculator_name {
            pub fn new() -> Self {
                Self {}
            }
        }

        impl ReclaimCalculator for $calculator_name {
            fn calculate(
                &mut self,
                config: &Config,
                args: &CalculationArgs,
            ) -> Option<(MemoryReclaim, MemoryReclaimReason)> {
                if args.psi_level >= PsiLevel::Background {
                    if let Some(metric_per_sec_kb) = calculate_per_sec_kb(
                        args.vmstat.$vmstat_property,
                        args.last_vmstat.$vmstat_property,
                        args.vmstat_duration,
                    ) {
                        if metric_per_sec_kb
                            >= config.$threshold_property * args.thrashing_threshold_multiplier
                        {
                            return Some((MemoryReclaim::Critical(metric_per_sec_kb), $reason));
                        }
                    }
                }
                None
            }
        }
    };
}

thrashing_reclaim_calculator!(
    ThrashingAnonCondition,
    workingset_refault_anon,
    thrashing_anon_threshold_kb,
    MemoryReclaimReason::ThrashingAnon
);
thrashing_reclaim_calculator!(
    ThrashingFileCondition,
    workingset_refault_file,
    thrashing_file_threshold_kb,
    MemoryReclaimReason::ThrashingFile
);
thrashing_reclaim_calculator!(
    DirectReclaimCondition,
    pgsteal_direct,
    pgsteal_direct_threshold_kb,
    MemoryReclaimReason::DirectReclaim
);
