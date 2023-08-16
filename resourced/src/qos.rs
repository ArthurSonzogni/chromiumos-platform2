// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// APIs to adjust the Quality of Service (QoS) expected for a thread or a
// process. QoS definitions map to performance characteristics.

use anyhow::{anyhow, bail, Context, Result};
use once_cell::sync::Lazy;
use procfs::process::Process;

use std::fs::write;

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

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ThreadSchedulerState {
    UrgentBursty = 0,
    Urgent = 1,
    Balanced = 2,
    Eco = 3,
    Utility = 4,
    Background = 5,
    Max = 6,
}

impl TryFrom<u8> for ThreadSchedulerState {
    type Error = anyhow::Error;

    fn try_from(mode_raw: u8) -> Result<ThreadSchedulerState> {
        Ok(match mode_raw {
            0 => ThreadSchedulerState::UrgentBursty,
            1 => ThreadSchedulerState::Urgent,
            2 => ThreadSchedulerState::Balanced,
            3 => ThreadSchedulerState::Eco,
            4 => ThreadSchedulerState::Utility,
            5 => ThreadSchedulerState::Background,
            _ => bail!("Unsupported thread state value"),
        })
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CpuSelection {
    All = 0,
    Efficient = 1,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct sched_attr {
    pub size: u32,

    pub sched_policy: u32,
    pub sched_flags: u64,
    pub sched_nice: i32,

    pub sched_priority: u32,

    pub sched_runtime: u64,
    pub sched_deadline: u64,
    pub sched_period: u64,

    pub sched_util_min: u32,
    pub sched_util_max: u32,
}

const SCHED_FLAG_UTIL_CLAMP_MIN: u64 = 0x20;
const SCHED_FLAG_UTIL_CLAMP_MAX: u64 = 0x40;

impl sched_attr {
    pub const fn default() -> Self {
        Self {
            size: std::mem::size_of::<sched_attr>() as u32,
            sched_policy: libc::SCHED_OTHER as u32,
            sched_flags: 0,
            sched_nice: 0,
            sched_priority: 0,
            sched_runtime: 0,
            sched_deadline: 0,
            sched_period: 0,
            sched_util_min: 0,
            sched_util_max: UCLAMP_MAX,
        }
    }
}

// Determine if uclamp is supported on this system.
static SUPPORTS_UCLAMP: Lazy<bool> = Lazy::new(|| {
    let mut temp_attr: sched_attr = sched_attr {
        sched_util_max: 0,
        ..sched_attr::default()
    };
    unsafe {
        libc::syscall(
            libc::SYS_sched_getattr,
            0, // current thread
            &mut temp_attr as *mut sched_attr as usize,
            std::mem::size_of::<sched_attr>() as u32,
            0,
        )
    };

    // shed_util_max is only filled in if uclamp is supported.
    return temp_attr.sched_util_max != 0;
});

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct ThreadSettings {
    sched_settings: sched_attr,
    cpuset: CpuSelection,
    // On systems that use EAS, EAS will try to pack workloads onto non-idle
    // cpus first as long as there is capacity. However, if an idle cpu was
    // chosen it would reduce the latency.
    prefer_idle: bool,
}

const CGROUP_NORMAL: &str = "/sys/fs/cgroup/cpu/resourced/normal/cgroup.procs";
const CGROUP_BACKGROUND: &str = "/sys/fs/cgroup/cpu/resourced/background/cgroup.procs";

// Note these might be changed to resourced specific folders in the futre
const CPUSET_ALL: &str = "/sys/fs/cgroup/cpuset/chrome/urgent/tasks";
const CPUSET_EFFICIENT: &str = "/sys/fs/cgroup/cpuset/chrome/non-urgent/tasks";

const UCLAMP_MAX: u32 = 1024;
const UCLAMP_BOOST_PERCENT: u32 = 60;
const UCLAMP_BOOSTED_MIN: u32 = (UCLAMP_BOOST_PERCENT * UCLAMP_MAX + 50) / 100;

// Thread QoS settings table
const THREAD_SETTINGS: [ThreadSettings; ThreadSchedulerState::Max as usize] = [
    // UrgentBursty
    ThreadSettings {
        sched_settings: sched_attr {
            sched_policy: libc::SCHED_FIFO as u32,
            sched_priority: 8,
            sched_util_min: UCLAMP_BOOSTED_MIN,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Urgent
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: -8,
            sched_util_min: UCLAMP_BOOSTED_MIN,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Balanced
    ThreadSettings {
        sched_settings: sched_attr {
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Eco
    ThreadSettings {
        sched_settings: sched_attr {
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
    // Utility
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: 1,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
    // Background
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: 10,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
];

fn is_same_process(process_id: i32, thread_id: i32) -> Result<bool> {
    let proc =
        Process::new(thread_id).map_err(|e| anyhow!("Failed to find process, error: {}", e))?;

    let stat = proc
        .status()
        .map_err(|e| anyhow!("Failed to find process status, error: {}", e))?;

    Ok(stat.tgid == process_id)
}

pub fn change_process_state(process_id: i32, process_state: ProcessSchedulerState) -> Result<()> {
    match process_state {
        ProcessSchedulerState::Normal => write(CGROUP_NORMAL, process_id.to_string())
            .context(format!("Failed to write to {}", CGROUP_NORMAL))?,
        ProcessSchedulerState::Background => write(CGROUP_BACKGROUND, process_id.to_string())
            .context(format!("Failed to write to {}", CGROUP_BACKGROUND))?,
    }

    Ok(())
}

pub fn change_thread_state(
    process_id: i32,
    thread_id: i32,
    thread_state: ThreadSchedulerState,
) -> Result<()> {
    // Validate thread_id is a thread of process_id
    if !is_same_process(process_id, thread_id)? {
        bail!("Thread does not belong to process");
    }

    let thread_settings = &THREAD_SETTINGS[thread_state as usize];
    let mut temp_sched_attr = thread_settings.sched_settings;
    temp_sched_attr.sched_flags |= if *SUPPORTS_UCLAMP {
        SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX
    } else {
        0
    };

    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_setattr,
            thread_id,
            &mut temp_sched_attr as *mut sched_attr as usize,
            0,
        )
    };
    if res < 0 {
        bail!(
            "Failed to set scheduler attributes, error={}",
            std::io::Error::last_os_error()
        );
    }

    // Apply the cpuset setting
    match thread_settings.cpuset {
        CpuSelection::All => write(CPUSET_ALL, thread_id.to_string())
            .context(format!("Failed to write to {}", CPUSET_ALL))?,
        CpuSelection::Efficient => write(CPUSET_EFFICIENT, thread_id.to_string())
            .context(format!("Failed to write to {}", CPUSET_EFFICIENT))?,
    };

    // Apply latency sensitive. Latency_sensitive will prefer idle cores.
    // This is a patch not yet in upstream(http://crrev/c/2981472)
    let latency_sensitive_file =
        format!("/proc/{}/task/{}/latency_sensitive", process_id, thread_id);

    if std::path::Path::new(&latency_sensitive_file).exists() {
        let value = if thread_settings.prefer_idle { 1 } else { 0 };
        write(&latency_sensitive_file, value.to_string())
            .context(format!("Failed to write to {}", latency_sensitive_file))?;
    }

    Ok(())
}
