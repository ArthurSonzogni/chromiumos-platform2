// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// APIs to adjust the Quality of Service (QoS) expected for a thread or a
// process. QoS definitions map to performance characteristics.

pub mod cgroups;
mod proc;
mod sched_attr;
#[cfg(test)]
mod test_utils;

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt::Display;
use std::io;

pub use cgroups::CgroupContext;
pub use cgroups::CpuCgroup;
pub use cgroups::CpusetCgroup;
use proc::load_process_timestamp;
use proc::load_thread_timestamp;
use proc::ThreadChecker;
use sched_attr::SchedAttrContext;
use sched_attr::UCLAMP_BOOSTED_MIN;
pub use sched_attr::UCLAMP_MAX;

pub type Result<T> = std::result::Result<T, Error>;

const NUM_PROCESS_STATES: usize = ProcessState::Background as usize + 1;
const NUM_THREAD_STATES: usize = ThreadState::Background as usize + 1;

/// Errors from schedqos crate.
#[derive(Debug)]
pub enum Error {
    Config(&'static str, &'static str),
    Cgroup(&'static str, io::Error),
    SchedAttr(io::Error),
    LatencySensitive(io::Error),
    Proc(proc::Error),
    ProcessNotFound,
    ThreadNotFound,
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Config(_, _) => None,
            Self::Cgroup(_, e) => Some(e),
            Self::SchedAttr(e) => Some(e),
            Self::LatencySensitive(e) => Some(e),
            Self::Proc(e) => Some(e),
            Self::ProcessNotFound => None,
            Self::ThreadNotFound => None,
        }
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Config(category, e) => f.write_fmt(format_args!("config: {category}: {e}")),
            Self::Cgroup(name, e) => f.write_fmt(format_args!("cgroup: {name}: {e}")),
            Self::SchedAttr(e) => f.write_fmt(format_args!("sched_setattr(2): {e}")),
            Self::LatencySensitive(e) => f.write_fmt(format_args!("latency sensitive file: {e}")),
            Self::Proc(e) => f.write_fmt(format_args!("procfs: {e}")),
            Self::ProcessNotFound => f.write_str("process not found"),
            Self::ThreadNotFound => f.write_str("thread not found"),
        }
    }
}

impl From<proc::Error> for Error {
    fn from(e: proc::Error) -> Self {
        Self::Proc(e)
    }
}

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
    pub cgroup_context: CgroupContext,
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

/// Wrap u32 PID with [ProcessId] internally.
///
/// Using u32 for both process id and thread id is confusing in this library.
/// This is to prevent unexpected typo by the explicit typing.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct ProcessId(u32);

/// Wrap u32 TID with [ThreadId] internally.
///
/// See [ProcessId] for the reason.
///
/// [std::thread::ThreadId] is not our use case because it is only for thread in
/// the running process and not expected for threads of other processes.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct ThreadId(u32);

pub struct ProcessKey {
    process_id: ProcessId,
    timestamp: u64,
}

#[derive(Debug)]
struct ProcessContext {
    state: ProcessState,
    timestamp: u64,
    threads: HashMap<ThreadId, ThreadContext>,
}

#[derive(Debug)]
struct ThreadContext {
    state: ThreadState,
    timestamp: u64,
}

pub struct SchedQosContext {
    config: Config,
    sched_attr_context: SchedAttrContext,
    state_map: HashMap<ProcessId, ProcessContext>,
}

impl SchedQosContext {
    pub fn new(config: Config) -> Result<Self> {
        for thread_config in &config.thread_configs {
            thread_config
                .validate()
                .map_err(|e| Error::Config("thread validation", e))?;
        }

        Ok(Self {
            config,
            sched_attr_context: SchedAttrContext::new().map_err(Error::SchedAttr)?,
            state_map: HashMap::new(),
        })
    }

    pub fn set_process_state(
        &mut self,
        process_id: u32,
        process_state: ProcessState,
    ) -> Result<Option<ProcessKey>> {
        let process_id = ProcessId(process_id);

        let new_process = match self.state_map.entry(process_id) {
            Entry::Occupied(mut entry) => {
                // We don't check whether the process is dead or not here
                // because processes are supposed to be removed via
                // remove_process() as soon as they die.
                entry.get_mut().state = process_state;
                None
            }
            Entry::Vacant(entry) => {
                let timestamp = match load_process_timestamp(process_id) {
                    Err(proc::Error::NotFound) => {
                        return Err(Error::ProcessNotFound);
                    }
                    other => other?,
                };
                let process_ctx = ProcessContext {
                    state: process_state,
                    timestamp,
                    threads: HashMap::new(),
                };
                Some((entry, process_ctx))
            }
        };

        let process_config = &self.config.process_configs[process_state as usize];
        self.config
            .cgroup_context
            .set_cpu_cgroup(process_id, process_config.cpu_cgroup)
            .map_err(|e| Error::Cgroup(process_config.cpu_cgroup.name(), e))?;
        // TODO(kawasin): Change threads settings.

        if let Some((entry, process_ctx)) = new_process {
            let timestamp = process_ctx.timestamp;
            entry.insert(process_ctx);
            Ok(Some(ProcessKey {
                process_id,
                timestamp,
            }))
        } else {
            Ok(None)
        }
    }

    /// Stop managing QoS state associated with the given [ProcessKey].
    pub fn remove_process(&mut self, process_key: ProcessKey) {
        if let Entry::Occupied(entry) = self.state_map.entry(process_key.process_id) {
            if entry.get().timestamp == process_key.timestamp {
                entry.remove();
            }
        }
    }

    pub fn set_thread_state(
        &mut self,
        process_id: u32,
        thread_id: u32,
        thread_state: ThreadState,
    ) -> Result<()> {
        let process_id = ProcessId(process_id);
        let thread_id = ThreadId(thread_id);

        let Entry::Occupied(mut entry) = self.state_map.entry(process_id) else {
            return Err(Error::ProcessNotFound);
        };
        let process = entry.get_mut();

        match process.threads.entry(thread_id) {
            Entry::Occupied(mut entry) => {
                let thread = entry.get_mut();
                let timestamp = match load_thread_timestamp(process_id, thread_id) {
                    Err(proc::Error::NotFound) => {
                        entry.remove();
                        return Err(Error::ThreadNotFound);
                    }
                    other => other?,
                };
                thread.timestamp = timestamp;
                thread.state = thread_state;
            }
            Entry::Vacant(_) => {
                // GC threads in the map before adding a new thread context to the map. This is for
                // the case a process spawns many short-term threads while the process state keeps
                // the same.
                {
                    let mut thread_checker = ThreadChecker::new(process_id);
                    process
                        .threads
                        .retain(|thread_id, _| thread_checker.thread_exists(*thread_id));
                }

                let timestamp = match load_thread_timestamp(process_id, thread_id) {
                    Err(proc::Error::NotFound) => {
                        return Err(Error::ThreadNotFound);
                    }
                    other => other?,
                };
                let thread = ThreadContext {
                    state: thread_state,
                    timestamp,
                };
                process.threads.insert(thread_id, thread);
            }
        };

        let thread_config = &self.config.thread_configs[thread_state as usize];

        self.sched_attr_context
            .set_thread_sched_attr(thread_id, thread_config)
            .map_err(Error::SchedAttr)?;

        self.config
            .cgroup_context
            .set_cpuset_cgroup(thread_id, thread_config.cpuset_cgroup)
            .map_err(|e| Error::Cgroup(thread_config.cpuset_cgroup.name(), e))?;

        // Apply latency sensitive. Latency_sensitive will prefer idle cores.
        // This is a patch not yet in upstream(http://crrev/c/2981472)
        let latency_sensitive_file = format!(
            "/proc/{}/task/{}/latency_sensitive",
            process_id.0, thread_id.0
        );

        if std::path::Path::new(&latency_sensitive_file).exists() {
            let value = if thread_config.prefer_idle { 1 } else { 0 };
            std::fs::write(&latency_sensitive_file, value.to_string())
                .map_err(Error::LatencySensitive)?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::*;

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
    fn test_set_process_state() {
        let (cgroup_context, mut cgroup_files) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: [
                ProcessStateConfig {
                    cpu_cgroup: CpuCgroup::Normal,
                },
                ProcessStateConfig {
                    cpu_cgroup: CpuCgroup::Background,
                },
            ],
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        let process_id = std::process::id();
        ctx.set_process_state(process_id, ProcessState::Normal)
            .unwrap();
        assert_eq!(read_number(&mut cgroup_files.cpu_normal), Some(process_id));

        ctx.set_process_state(process_id, ProcessState::Background)
            .unwrap();
        assert_eq!(
            read_number(&mut cgroup_files.cpu_background),
            Some(process_id)
        );
    }

    #[test]
    fn test_set_process_state_invalid_process() {
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        let (process_id, _, process) = fork_process_for_test();
        drop(process);
        assert!(matches!(
            ctx.set_process_state(process_id.0, ProcessState::Normal)
                .err()
                .unwrap(),
            Error::ProcessNotFound
        ));
    }

    #[test]
    fn test_set_process_state_for_new_process() {
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        let process_id = std::process::id();

        // First set_process_state() creates a new process context.
        let process_key = ctx
            .set_process_state(process_id, ProcessState::Normal)
            .unwrap();
        assert!(process_key.is_some());
        let process_key = process_key.unwrap();
        assert_eq!(process_key.process_id, ProcessId(process_id));
        assert_eq!(ctx.state_map.len(), 1);

        let process_key = ctx
            .set_process_state(process_id, ProcessState::Background)
            .unwrap();
        assert!(process_key.is_none());
        let process_key = ctx
            .set_process_state(process_id, ProcessState::Normal)
            .unwrap();
        assert!(process_key.is_none());

        let (process_id, _, _process) = fork_process_for_test();
        let process_key = ctx
            .set_process_state(process_id.0, ProcessState::Normal)
            .unwrap();
        assert!(process_key.is_some());
        let process_key = process_key.unwrap();
        assert_eq!(process_key.process_id, process_id);
        assert_eq!(ctx.state_map.len(), 2);
    }

    #[test]
    fn test_remove_process() {
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        let mut processes = Vec::new();
        for _ in 0..3 {
            let (process_id, _, process) = fork_process_for_test();
            processes.push(process);
            ctx.set_process_state(process_id.0, ProcessState::Normal)
                .unwrap();
        }

        let (process_id, _, process) = fork_process_for_test();
        let process_key = ctx
            .set_process_state(process_id.0, ProcessState::Normal)
            .unwrap()
            .unwrap();
        assert_eq!(ctx.state_map.len(), 4);

        // Process is not cloneable. This is for testing purpose.
        let cloned_process_key = ProcessKey {
            process_id,
            timestamp: process_key.timestamp,
        };

        drop(process);

        ctx.remove_process(process_key);
        assert_eq!(ctx.state_map.len(), 3);

        // If the process is removed, remove_process() is no-op.
        ctx.remove_process(cloned_process_key);
        assert_eq!(ctx.state_map.len(), 3);
    }

    #[test]
    fn test_set_thread_state() {
        let process_id = std::process::id();
        let (cgroup_context, mut cgroup_files) = create_fake_cgroup_context_pair();
        let thread_configs = [
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
        ];
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: thread_configs.clone(),
        })
        .unwrap();

        // set_process_state() is required before set_thread_state().
        ctx.set_process_state(process_id, ProcessState::Normal)
            .unwrap();

        let sched_ctx = SchedAttrContext::new().unwrap();

        for state in [
            ThreadState::UrgentBursty,
            ThreadState::Urgent,
            ThreadState::Balanced,
            ThreadState::Eco,
            ThreadState::Utility,
            ThreadState::Background,
        ] {
            let (thread_id, _thread) = spawn_thread_for_test();

            ctx.set_thread_state(process_id, thread_id.0, state)
                .unwrap();
            let thread_config = &thread_configs[state as usize];
            match thread_config.cpuset_cgroup {
                CpusetCgroup::All => {
                    assert_eq!(read_number(&mut cgroup_files.cpuset_all), Some(thread_id.0))
                }
                CpusetCgroup::Efficient => {
                    assert_eq!(
                        read_number(&mut cgroup_files.cpuset_efficient),
                        Some(thread_id.0)
                    )
                }
            }
            assert_sched_attr(&sched_ctx, thread_id, thread_config);
        }
    }

    #[test]
    fn test_set_thread_state_without_process() {
        let process_id = std::process::id();
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        let (thread_id, _thread) = spawn_thread_for_test();

        assert!(matches!(
            ctx.set_thread_state(process_id, thread_id.0, ThreadState::Balanced)
                .err()
                .unwrap(),
            Error::ProcessNotFound
        ));
    }

    #[test]
    fn test_set_thread_state_invalid_thread() {
        let process_id = ProcessId(std::process::id());
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();
        let (_, child_process_thread_id, _process) = fork_process_for_test();
        let (thread_id, thread) = spawn_thread_for_test();

        ctx.set_process_state(process_id.0, ProcessState::Normal)
            .unwrap();

        // The thread does not in the process.
        assert!(matches!(
            ctx.set_thread_state(
                process_id.0,
                child_process_thread_id.0,
                ThreadState::Balanced
            )
            .err()
            .unwrap(),
            Error::ThreadNotFound
        ));

        // The thread is dead.
        drop(thread);
        assert!(wait_for_thread_removed(process_id, thread_id));
        assert!(matches!(
            ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
                .err()
                .unwrap(),
            Error::ThreadNotFound
        ));

        let (thread_id, thread) = spawn_thread_for_test();
        ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
            .unwrap();
        // The thread is dead after registered.
        drop(thread);
        assert!(wait_for_thread_removed(process_id, thread_id));
        assert!(matches!(
            ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
                .err()
                .unwrap(),
            Error::ThreadNotFound
        ));
    }

    #[test]
    fn test_set_thread_state_gc() {
        let process_id = ProcessId(std::process::id());
        let (cgroup_context, _) = create_fake_cgroup_context_pair();
        let mut ctx = SchedQosContext::new(Config {
            cgroup_context,
            process_configs: Config::default_process_config(),
            thread_configs: Config::default_thread_config(),
        })
        .unwrap();

        ctx.set_process_state(process_id.0, ProcessState::Normal)
            .unwrap();

        let (thread_id, _thread1) = spawn_thread_for_test();
        ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
            .unwrap();
        let process_ctx = ctx.state_map.get(&process_id).unwrap();
        assert_eq!(process_ctx.threads.len(), 1);

        for _ in 0..10 {
            let (thread_id, thread) = spawn_thread_for_test();
            ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
                .unwrap();
            drop(thread);
            wait_for_thread_removed(process_id, thread_id);
        }

        let (thread_id, _thread2) = spawn_thread_for_test();
        ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
            .unwrap();
        let process_ctx = ctx.state_map.get(&process_id).unwrap();
        assert_eq!(process_ctx.threads.len(), 2);

        let (thread_id, _thread3) = spawn_thread_for_test();
        ctx.set_thread_state(process_id.0, thread_id.0, ThreadState::Balanced)
            .unwrap();
        let process_ctx = ctx.state_map.get(&process_id).unwrap();
        assert_eq!(process_ctx.threads.len(), 3);
    }
}
