// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// APIs to adjust the Quality of Service (QoS) expected for a thread or a
// process. QoS definitions map to performance characteristics.

use anyhow::bail;
use anyhow::Result;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ProcessSchedulerState {
    Normal = 0,
    Background = 1,
}

impl TryFrom<u8> for ProcessSchedulerState {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<ProcessSchedulerState> {
        Ok(match mode_raw {
            0 => ProcessSchedulerState::Normal,
            1 => ProcessSchedulerState::Background,
            _ => bail!("Unsupported process state value"),
        })
    }
}

static CGROUP_NORMAL: &str = "/sys/fs/cgroup/cpu/cgroup.procs";
static CGROUP_BACKGROUND: &str = "/sys/fs/cgroup/cpu/chrome_renderers/background/cgroup.procs";

pub fn change_process_state(process_id: i32, process_state: ProcessSchedulerState) -> Result<()> {
    match process_state {
        ProcessSchedulerState::Normal => std::fs::write(CGROUP_NORMAL, process_id.to_string())?,
        ProcessSchedulerState::Background => {
            std::fs::write(CGROUP_BACKGROUND, process_id.to_string())?
        }
    }

    Ok(())
}
