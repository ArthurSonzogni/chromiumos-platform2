// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;
use std::time::Duration;

use anyhow::Result;
use log::error;

use crate::common::read_from_file;

const SCHED_BASE_SLICE_PATH: &str = "/sys/kernel/debug/sched/base_slice_ns";

const BASE_SLICE_MIN: Duration = Duration::from_millis(6);

pub fn init_cpu_config() -> Result<()> {
    // EEVDF scheduler was added upstream in 6.6 to replace CFS. The scheudler
    // is a virtual deadline based scheduler.
    // base_slice_ns controls the minimum slice that a task will run for before
    // being switched. Based on our testing, anything below 6ms will result in
    // some performance regressions.
    let base_slice_min = BASE_SLICE_MIN.as_nanos() as u64;
    let base_slice_ns: u64 =
        read_from_file(&PathBuf::from(SCHED_BASE_SLICE_PATH)).unwrap_or(base_slice_min);
    if base_slice_ns < base_slice_min {
        if let Err(e) = std::fs::write(SCHED_BASE_SLICE_PATH, base_slice_min.to_string().as_bytes())
        {
            error!("Failed to write {} {}\n", SCHED_BASE_SLICE_PATH, e);
        };
    }

    Ok(())
}
