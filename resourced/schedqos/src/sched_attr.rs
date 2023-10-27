// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;

use crate::ThreadId;
use crate::ThreadStateConfig;

const SCHED_FLAG_UTIL_CLAMP_MIN: u64 = 0x20;
const SCHED_FLAG_UTIL_CLAMP_MAX: u64 = 0x40;

/// The maximum value for uclamp
pub const UCLAMP_MAX: u32 = 1024;
const UCLAMP_BOOST_PERCENT: u32 = 60;
pub const UCLAMP_BOOSTED_MIN: u32 = (UCLAMP_BOOST_PERCENT * UCLAMP_MAX + 50) / 100;

/// Context to apply sched_attr.
pub struct SchedAttrContext {
    uclamp_support: bool,
}

impl SchedAttrContext {
    /// Initialize [SchedAttrContext].
    pub fn new() -> io::Result<Self> {
        Ok(Self {
            uclamp_support: check_uclamp_support()?,
        })
    }

    pub(crate) fn set_thread_sched_attr(
        &self,
        thread_id: ThreadId,
        thread_config: &ThreadStateConfig,
    ) -> io::Result<()> {
        let mut attr = sched_attr::default();

        sched_getattr(thread_id, &mut attr)?;

        if let Some(rt_priority) = thread_config.rt_priority {
            attr.sched_policy = libc::SCHED_FIFO as u32;
            attr.sched_priority = rt_priority;
        } else {
            attr.sched_policy = libc::SCHED_OTHER as u32;
            // sched_priority must be cleared. Otherwise sched_setattr(2) fails
            // as EINVAL.
            attr.sched_priority = 0;
        }
        attr.sched_nice = thread_config.nice;

        // Setting SCHED_FLAG_UTIL_CLAMP_MIN or SCHED_FLAG_UTIL_CLAMP_MAX should
        // be avoided if kernel does not support uclamp. Otherwise
        // sched_setattr(2) fails as EOPNOTSUPP.
        if self.uclamp_support {
            attr.sched_util_max = UCLAMP_MAX;
            attr.sched_util_min = thread_config.uclamp_min;
            attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;
        };

        sched_setattr(thread_id, &mut attr)
    }
}

/// sched_attr defined in Linux.
///
/// See `/include/uapi/linux/sched/types.h` of the Linux kernel.
#[repr(C)]
#[derive(Debug)]
struct sched_attr {
    size: u32,

    sched_policy: u32,
    sched_flags: u64,
    sched_nice: i32,

    sched_priority: u32,

    sched_runtime: u64,
    sched_deadline: u64,
    sched_period: u64,

    sched_util_min: u32,
    sched_util_max: u32,
}

impl Default for sched_attr {
    fn default() -> Self {
        Self {
            size: std::mem::size_of::<sched_attr>() as u32,
            sched_policy: 0,
            sched_flags: 0,
            sched_nice: 0,
            sched_priority: 0,
            sched_runtime: 0,
            sched_deadline: 0,
            sched_period: 0,
            sched_util_min: 0,
            sched_util_max: 0,
        }
    }
}

/// Rust wrapper of sched_getattr(2).
fn sched_getattr(thread_id: ThreadId, attr: &mut sched_attr) -> io::Result<()> {
    // SAFETY: sched_getattr only modifies fields of attr.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_getattr,
            thread_id.0,
            attr as *mut sched_attr as usize,
            std::mem::size_of::<sched_attr>() as u32,
            0,
        )
    };
    if res < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

/// Rust wrapper of sched_setattr(2).
///
/// The argument attr is not modified on current kernel implementation, but
/// sched_setattr(2) is defined to accept sched_attr as non-const in the man
/// page. The attr of this wrapper also is marked as mutable to comply with the
/// syscall definition.
fn sched_setattr(thread_id: ThreadId, attr: &mut sched_attr) -> io::Result<()> {
    // SAFETY: sched_setattr does not modify userspace memory.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_setattr,
            thread_id.0,
            attr as *mut sched_attr as usize,
            0,
        )
    };
    if res < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

/// Check the kernel support setting uclamp via sched_attr.
///
/// sched_util_min and sched_util_max were added to sched_attr from Linux kernel
/// v5.3 and guarded by CONFIG_UCLAMP_TASK flag.
fn check_uclamp_support() -> io::Result<bool> {
    let self_thread_id = ThreadId(0);
    let mut attr = sched_attr::default();

    // sched_getattr must succeeds in most cases.
    //
    // * no ESRCH because this is inqury for this thread.
    // * no E2BIG nor EINVAL because sched_attr struct must be correct.
    //   Otherwise following sched_setattr fail anyway.
    //
    // Some environments (e.g. qemu-user) do not support sched_getattr(2)
    // and may fail as ENOSYS.
    sched_getattr(self_thread_id, &mut attr)?;

    attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;

    match sched_setattr(self_thread_id, &mut attr) {
        Ok(()) => Ok(true),
        Err(e) => {
            if e.raw_os_error() == Some(libc::EOPNOTSUPP) {
                Ok(false)
            } else {
                Err(e)
            }
        }
    }
}

#[cfg(test)]
pub(crate) fn assert_sched_attr(
    ctx: &SchedAttrContext,
    thread_id: ThreadId,
    thread_config: &ThreadStateConfig,
) {
    let mut attr = sched_attr::default();
    sched_getattr(thread_id, &mut attr).unwrap();

    if let Some(rt_priority) = thread_config.rt_priority {
        assert_eq!(attr.sched_policy, libc::SCHED_FIFO as u32);
        assert_eq!(attr.sched_priority, rt_priority);
    } else {
        assert_eq!(attr.sched_policy, libc::SCHED_OTHER as u32);
        assert_eq!(attr.sched_nice, thread_config.nice);
    }

    if ctx.uclamp_support {
        assert_eq!(attr.sched_util_max, UCLAMP_MAX);
        assert_eq!(attr.sched_util_min, thread_config.uclamp_min);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cgroups::CpusetCgroup;

    struct ScopedSchedAttrRestore {
        thread_id: ThreadId,
        original_attr: sched_attr,
    }

    impl ScopedSchedAttrRestore {
        fn new(thread_id: ThreadId) -> Self {
            let mut original_attr = sched_attr::default();
            sched_getattr(thread_id, &mut original_attr).unwrap();
            Self {
                thread_id,
                original_attr,
            }
        }
    }

    impl Drop for ScopedSchedAttrRestore {
        fn drop(&mut self) {
            sched_setattr(self.thread_id, &mut self.original_attr).unwrap();
        }
    }

    #[test]
    fn test_set_thread_sched_attr() {
        let ctx = SchedAttrContext::new().unwrap();
        let _original_thread_attr = ScopedSchedAttrRestore::new(ThreadId(0));

        for (nice, uclamp_min) in [(11, 12), (13, 14), (-8, 0)] {
            let thread_config = ThreadStateConfig {
                rt_priority: None,
                nice,
                uclamp_min,
                cpuset_cgroup: CpusetCgroup::All,
                prefer_idle: false,
            };

            ctx.set_thread_sched_attr(ThreadId(0), &thread_config)
                .unwrap();

            let mut attr = sched_attr::default();
            sched_getattr(ThreadId(0), &mut attr).unwrap();
            assert_eq!(attr.sched_policy, libc::SCHED_OTHER as u32);
            assert_eq!(attr.sched_nice, nice);

            if ctx.uclamp_support {
                assert_eq!(attr.sched_util_max, UCLAMP_MAX);
                assert_eq!(attr.sched_util_min, uclamp_min);
            }
            assert_sched_attr(&ctx, ThreadId(0), &thread_config);
        }
    }

    #[test]
    fn test_set_thread_sched_attr_rt() {
        let ctx = SchedAttrContext::new().unwrap();
        let _original_thread_attr = ScopedSchedAttrRestore::new(ThreadId(0));

        for (nice, uclamp_min, rt_priority) in [(11, 12, 13), (14, 15, 16), (-8, 0, 1)] {
            let thread_config = ThreadStateConfig {
                rt_priority: Some(rt_priority),
                nice,
                uclamp_min,
                cpuset_cgroup: CpusetCgroup::All,
                prefer_idle: false,
            };

            ctx.set_thread_sched_attr(ThreadId(0), &thread_config)
                .unwrap();

            let mut attr = sched_attr::default();
            sched_getattr(ThreadId(0), &mut attr).unwrap();
            assert_eq!(attr.sched_policy, libc::SCHED_FIFO as u32);
            assert_eq!(attr.sched_priority, rt_priority);
            assert_eq!(attr.sched_nice, 0);

            if ctx.uclamp_support {
                assert_eq!(attr.sched_util_max, UCLAMP_MAX);
                assert_eq!(attr.sched_util_min, uclamp_min);
            }
            assert_sched_attr(&ctx, ThreadId(0), &thread_config);
        }
    }

    #[test]
    fn test_set_thread_sched_attr_remove_rt() {
        let ctx = SchedAttrContext::new().unwrap();
        let _original_thread_attr = ScopedSchedAttrRestore::new(ThreadId(0));

        ctx.set_thread_sched_attr(
            ThreadId(0),
            &ThreadStateConfig {
                rt_priority: Some(10),
                ..ThreadStateConfig::default()
            },
        )
        .unwrap();
        // sched_priority must be cleared. Otherwise sched_setattr(2) fails as
        // EINVAL.
        assert!(ctx
            .set_thread_sched_attr(
                ThreadId(0),
                &ThreadStateConfig {
                    rt_priority: None,
                    ..ThreadStateConfig::default()
                }
            )
            .is_ok());
    }
}
