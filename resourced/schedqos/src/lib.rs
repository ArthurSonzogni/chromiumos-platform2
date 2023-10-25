// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// APIs to adjust the Quality of Service (QoS) expected for a thread or a
// process. QoS definitions map to performance characteristics.

use std::io;
use std::path::Path;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;

const NUM_PROCESS_STATES: usize = ProcessState::Background as usize + 1;
const NUM_THREAD_STATES: usize = ThreadState::Background as usize + 1;

/// Scheduler QoS states of a process.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ProcessState {
    Normal = 0,
    Background = 1,
}

impl TryFrom<u8> for ProcessState {
    type Error = ();

    fn try_from(v: u8) -> std::result::Result<Self, Self::Error> {
        match v {
            0 => Ok(Self::Normal),
            1 => Ok(Self::Background),
            _ => Err(()),
        }
    }
}

/// Scheduler QoS states of a thread.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ThreadState {
    UrgentBursty = 0,
    Urgent = 1,
    Balanced = 2,
    Eco = 3,
    Utility = 4,
    Background = 5,
}

impl TryFrom<u8> for ThreadState {
    type Error = ();

    fn try_from(v: u8) -> std::result::Result<Self, Self::Error> {
        match v {
            0 => Ok(Self::UrgentBursty),
            1 => Ok(Self::Urgent),
            2 => Ok(Self::Balanced),
            3 => Ok(Self::Eco),
            4 => Ok(Self::Utility),
            5 => Ok(Self::Background),
            _ => Err(()),
        }
    }
}

/// Config of each process/thread QoS state.
#[derive(Debug)]
pub struct Config {
    /// Config for cgroups
    pub cgroup_config: CgroupConfig,
    /// ProcessStateConfig for each process QoS state
    pub process_configs: [ProcessStateConfig; NUM_PROCESS_STATES],
    /// ThreadStateConfig for each thread QoS state
    pub thread_configs: [ThreadStateConfig; NUM_THREAD_STATES],
}

impl Config {
    pub const fn default_process_config() -> [ProcessStateConfig; NUM_PROCESS_STATES] {
        [
            // ProcessState::Normal
            ProcessStateConfig {
                cpu_cgroup: CpuCgroup::Normal,
            },
            // Process:State::Background
            ProcessStateConfig {
                cpu_cgroup: CpuCgroup::Background,
            },
        ]
    }

    pub const fn default_thread_config() -> [ThreadStateConfig; NUM_THREAD_STATES] {
        [
            // ThreadState::UrgentBursty
            ThreadStateConfig {
                rt_priority: Some(8),
                nice: -8,
                uclamp_min: UCLAMP_BOOSTED_MIN,
                cpuset_cgroup: CpusetCgroup::All,
                prefer_idle: true,
            },
            // ThreadState::Urgent
            ThreadStateConfig {
                nice: -8,
                uclamp_min: UCLAMP_BOOSTED_MIN,
                cpuset_cgroup: CpusetCgroup::All,
                prefer_idle: true,
                ..ThreadStateConfig::default()
            },
            // ThreadState::Balanced
            ThreadStateConfig {
                cpuset_cgroup: CpusetCgroup::All,
                prefer_idle: true,
                ..ThreadStateConfig::default()
            },
            // ThreadState::Eco
            ThreadStateConfig {
                cpuset_cgroup: CpusetCgroup::Efficient,
                ..ThreadStateConfig::default()
            },
            // ThreadState::Utility
            ThreadStateConfig {
                nice: 1,
                cpuset_cgroup: CpusetCgroup::Efficient,
                ..ThreadStateConfig::default()
            },
            // ThreadState::Background
            ThreadStateConfig {
                nice: 10,
                cpuset_cgroup: CpusetCgroup::Efficient,
                ..ThreadStateConfig::default()
            },
        ]
    }
}

/// Paths to cgroup directories.
#[derive(Debug)]
pub struct CgroupConfig {
    /// Path to cpu cgroup for normal processes
    pub cpu_normal: PathBuf,
    /// Path to cpu cgroup for background processes
    pub cpu_background: PathBuf,
    /// Path to cpuset cgroup using all CPU cores
    pub cpuset_all: PathBuf,
    /// Path to cpuset cgroup using efficient CPU cores only
    pub cpuset_efficient: PathBuf,
}

/// Detailed scheduler settings for a process QoS state.
#[derive(Clone, Debug)]
pub struct ProcessStateConfig {
    /// The cpu cgroup
    pub cpu_cgroup: CpuCgroup,
}

/// Detailed scheduler settings for a thread QoS state.
#[derive(Clone, Debug)]
pub struct ThreadStateConfig {
    /// The priority in RT (SCHED_FIFO). If this is None, it uses SCHED_OTHER instead.
    pub rt_priority: Option<u32>,
    /// The nice value
    pub nice: i32,
    /// sched_attr.sched_util_min
    ///
    /// This must be smaller than or equal to 1024.
    pub uclamp_min: u32,
    /// The cpuset cgroup
    pub cpuset_cgroup: CpusetCgroup,
    /// On systems that use EAS, EAS will try to pack workloads onto non-idle
    /// cpus first as long as there is capacity. However, if an idle cpu was
    /// chosen it would reduce the latency.
    pub prefer_idle: bool,
}

impl ThreadStateConfig {
    fn validate(&self) -> std::result::Result<(), &'static str> {
        if self.uclamp_min > UCLAMP_MAX {
            return Err("uclamp_min is too big");
        }
        Ok(())
    }

    const fn default() -> Self {
        ThreadStateConfig {
            rt_priority: None,
            nice: 0,
            uclamp_min: 0,
            cpuset_cgroup: CpusetCgroup::All,
            prefer_idle: false,
        }
    }
}

/// Cpu cgroups
#[derive(Clone, Copy, Debug)]
pub enum CpuCgroup {
    Normal,
    Background,
}

/// Cpuset cgroups
#[derive(Clone, Copy, Debug)]
pub enum CpusetCgroup {
    All,
    Efficient,
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

/// Check the kernel support setting uclamp via sched_attr.
///
/// sched_util_min and sched_util_max were added to sched_attr from Linux kernel
/// v5.3 and guarded by CONFIG_UCLAMP_TASK flag.
fn check_uclamp_support() -> io::Result<bool> {
    let mut attr = sched_attr::default();

    // SAFETY: sched_getattr only modifies fields of attr.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_getattr,
            0, // current thread
            &mut attr as *mut sched_attr as usize,
            std::mem::size_of::<sched_attr>() as u32,
            0,
        )
    };

    if res < 0 {
        // sched_getattr must succeeds in most cases.
        //
        // * no ESRCH because this is inqury for this thread.
        // * no E2BIG nor EINVAL because sched_attr struct must be correct.
        //   Otherwise following sched_setattr fail anyway.
        //
        // Some environments (e.g. qemu-user) do not support sched_getattr(2)
        // and may fail as ENOSYS.
        return Err(io::Error::last_os_error());
    }

    attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;

    // SAFETY: sched_setattr does not modify userspace memory.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_setattr,
            0, // current thread
            &mut attr as *mut sched_attr as usize,
            0,
        )
    };

    if res < 0 {
        let err = io::Error::last_os_error();
        if err.raw_os_error() == Some(libc::EOPNOTSUPP) {
            Ok(false)
        } else {
            Err(err)
        }
    } else {
        Ok(true)
    }
}

const UCLAMP_MAX: u32 = 1024;
const UCLAMP_BOOST_PERCENT: u32 = 60;
const UCLAMP_BOOSTED_MIN: u32 = (UCLAMP_BOOST_PERCENT * UCLAMP_MAX + 50) / 100;

/// Wrap u32 PID with [ProcessId] internally.
///
/// Using u32 for both process id and thread id is confusing in this library.
/// This is to prevent unexpected typo by the explicit typing.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct ProcessId(u32);

/// Wrap u32 TID with [ThreadId] internally.
///
/// See [ProcessId] for the reason.
///
/// [std::thread::ThreadId] is not our use case because it is only for thread in
/// the running process and not expected for threads of other processes.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct ThreadId(u32);

fn validate_thread_id(process_id: ProcessId, thread_id: ThreadId) -> bool {
    Path::new(&format!("/proc/{}/task/{}", process_id.0, thread_id.0)).exists()
}

pub struct SchedQosContext {
    // TODO(kawasin): Convert cgroup path to a opened file.
    config: Config,
    uclamp_support: bool,
}

impl SchedQosContext {
    pub fn new(config: Config) -> anyhow::Result<Self> {
        for thread_config in &config.thread_configs {
            thread_config
                .validate()
                .map_err(|e| anyhow::anyhow!("thread: {}", e))?;
        }

        Ok(Self {
            config,
            uclamp_support: check_uclamp_support()?,
        })
    }

    pub fn set_process_state(
        // TODO(kawasin): Make this mut to update internal state mapping.
        &self,
        process_id: u32,
        process_state: ProcessState,
    ) -> Result<()> {
        let process_id = ProcessId(process_id);
        let process_config = &self.config.process_configs[process_state as usize];

        let cgroup = match process_config.cpu_cgroup {
            CpuCgroup::Normal => &self.config.cgroup_config.cpu_normal,
            CpuCgroup::Background => &self.config.cgroup_config.cpu_background,
        };

        std::fs::write(cgroup, process_id.0.to_string())
            .with_context(|| format!("Failed to write to {:?}", cgroup))
    }

    pub fn set_thread_state(
        // TODO(kawasin): Make this mut to update internal state mapping.
        &self,
        process_id: u32,
        thread_id: u32,
        thread_state: ThreadState,
    ) -> Result<()> {
        let process_id = ProcessId(process_id);
        let thread_id = ThreadId(thread_id);
        if !validate_thread_id(process_id, thread_id) {
            bail!("Thread does not belong to process");
        }

        let thread_config = &self.config.thread_configs[thread_state as usize];

        let cgroup = match thread_config.cpuset_cgroup {
            CpusetCgroup::All => &self.config.cgroup_config.cpuset_all,
            CpusetCgroup::Efficient => &self.config.cgroup_config.cpuset_efficient,
        };

        let mut attr = sched_attr::default();

        if let Some(rt_priority) = thread_config.rt_priority {
            attr.sched_policy = libc::SCHED_FIFO as u32;
            attr.sched_priority = rt_priority;
        }

        attr.sched_nice = thread_config.nice;

        // Setting SCHED_FLAG_UTIL_CLAMP_MIN or SCHED_FLAG_UTIL_CLAMP_MAX should
        // be avoided if kernel does not support uclamp. Otherwise
        // sched_setattr(2) fails as EOPNOTSUPP.
        if self.uclamp_support {
            attr.sched_util_min = thread_config.uclamp_min;
            attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;
        };

        let res = unsafe {
            libc::syscall(
                libc::SYS_sched_setattr,
                thread_id.0,
                &mut attr as *mut sched_attr as usize,
                0,
            )
        };
        if res < 0 {
            bail!(
                "Failed to set scheduler attributes, error={}",
                io::Error::last_os_error()
            );
        }

        std::fs::write(cgroup, thread_id.0.to_string())
            .context(format!("Failed to write to {:?}", cgroup))?;

        // Apply latency sensitive. Latency_sensitive will prefer idle cores.
        // This is a patch not yet in upstream(http://crrev/c/2981472)
        let latency_sensitive_file = format!(
            "/proc/{}/task/{}/latency_sensitive",
            process_id.0, thread_id.0
        );

        if std::path::Path::new(&latency_sensitive_file).exists() {
            let value = if thread_config.prefer_idle { 1 } else { 0 };
            std::fs::write(&latency_sensitive_file, value.to_string())
                .context(format!("Failed to write to {}", latency_sensitive_file))?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::*;

    #[test]
    fn test_process_state_conversion() {
        for state in [ProcessState::Normal, ProcessState::Background] {
            assert_eq!(state, ProcessState::try_from(state as u8).unwrap());
        }

        assert!(ProcessState::try_from(NUM_PROCESS_STATES as u8).is_err());
    }

    #[test]
    fn test_thread_state_conversion() {
        for state in [
            ThreadState::UrgentBursty,
            ThreadState::Urgent,
            ThreadState::Balanced,
            ThreadState::Eco,
            ThreadState::Utility,
            ThreadState::Background,
        ] {
            assert_eq!(state, ThreadState::try_from(state as u8).unwrap());
        }

        assert!(ThreadState::try_from(NUM_THREAD_STATES as u8).is_err());
    }

    #[test]
    fn test_validate_thread_id() {
        let process_id = ProcessId(std::process::id());
        let thread_id = ThreadId(unsafe { libc::gettid() } as u32);
        let child_process_id = unsafe { libc::fork() };
        if child_process_id == 0 {
            std::thread::sleep(Duration::from_secs(100));
            std::process::exit(0);
        }
        assert!(child_process_id > 0);
        let child_process_id = ProcessId(child_process_id as u32);
        let child_process_thread_id = ThreadId(child_process_id.0);

        assert!(validate_thread_id(process_id, thread_id));
        assert!(!validate_thread_id(process_id, child_process_thread_id));
        assert!(validate_thread_id(
            child_process_id,
            child_process_thread_id
        ));
        assert!(!validate_thread_id(child_process_id, thread_id));

        let child_process_id = child_process_id.0 as libc::pid_t;
        unsafe {
            libc::kill(child_process_id, libc::SIGKILL);
            libc::waitpid(child_process_id, std::ptr::null_mut(), 0);
        }
    }
}
