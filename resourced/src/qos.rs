// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;
use std::os::fd::FromRawFd;
use std::os::fd::OwnedFd;
use std::sync::Arc;
use std::sync::Mutex;

use anyhow::anyhow;
use log::error;
use schedqos::cgroups::open_cpuset_cgroup;
use schedqos::cgroups::setup_cpu_cgroup;
use schedqos::CgroupContext;
use schedqos::Config;
use schedqos::ProcessKey;
use schedqos::ProcessState;
use schedqos::SchedQosContext;
use tokio::io::unix::AsyncFd;
use tokio::io::Interest;
use tokio::task::JoinHandle;

pub fn create_schedqos_context() -> anyhow::Result<SchedQosContext> {
    let cpu_normal = setup_cpu_cgroup("resourced/normal", 1024)?;
    let cpu_background = setup_cpu_cgroup("resourced/background", 10)?;
    // Note these might be changed to resourced specific folders in the futre
    let cpuset_all = open_cpuset_cgroup("chrome/urgent")?;
    let cpuset_efficient = open_cpuset_cgroup("chrome/non-urgent")?;

    let ctx = SchedQosContext::new(Config {
        cgroup_context: CgroupContext {
            cpu_normal,
            cpu_background,
            cpuset_all,
            cpuset_efficient,
        },
        process_configs: Config::default_process_config(),
        thread_configs: Config::default_thread_config(),
    })?;
    Ok(ctx)
}

/// The returned [JoinHandle] is used for testing purpose.
pub fn set_process_state(
    sched_ctx: Arc<Mutex<SchedQosContext>>,
    process_id: u32,
    state: ProcessState,
) -> anyhow::Result<Option<JoinHandle<()>>> {
    let mut ctx = sched_ctx.lock().expect("lock schedqos context");

    match ctx.set_process_state(process_id, state) {
        Ok(Some(process_key)) => match create_async_pidfd(process_id) {
            Ok(pidfd) => Ok(Some(monitor_process(sched_ctx.clone(), pidfd, process_key))),
            Err(e) => {
                ctx.remove_process(process_key);
                Err(anyhow!("create async pidfd: {:?}", e))
            }
        },
        Ok(None) => Ok(None),
        Err(e) => Err(anyhow!("set process state: {:?}", e)),
    }
}

fn create_async_pidfd(pid: u32) -> std::io::Result<AsyncFd<OwnedFd>> {
    // SAFETY: pidfd_open(2) does not modify userspace memory.
    let res = unsafe { libc::syscall(libc::SYS_pidfd_open, pid, 0) } as libc::c_int;

    if res < 0 {
        return Err(io::Error::last_os_error());
    }

    // SAFETY:: The new pidfd is not owned by anything.
    let pidfd = unsafe { OwnedFd::from_raw_fd(res) };

    AsyncFd::with_interest(pidfd, Interest::READABLE)
}

fn monitor_process(
    sched_ctx: Arc<Mutex<SchedQosContext>>,
    pidfd: AsyncFd<OwnedFd>,
    process: ProcessKey,
) -> JoinHandle<()> {
    tokio::spawn(async move {
        match pidfd.readable().await {
            Ok(_guard) => {}
            Err(e) => {
                error!("pidfd readable fails: {:?}", e);
            }
        };
        sched_ctx
            .lock()
            .expect("lock schedqos context")
            .remove_process(process);
    })
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::*;
    use crate::test_utils::tests::*;

    // sched_getattr(2) is not supported on qemu-user which CQ uses to run tests for non-x86_64
    // boards.
    #[cfg(target_arch = "x86_64")]
    #[tokio::test]
    async fn test_set_process_state() {
        let sched_ctx = Arc::new(Mutex::new(
            SchedQosContext::new(Config {
                cgroup_context: CgroupContext {
                    cpu_normal: tempfile::tempfile().unwrap(),
                    cpu_background: tempfile::tempfile().unwrap(),
                    cpuset_all: tempfile::tempfile().unwrap(),
                    cpuset_efficient: tempfile::tempfile().unwrap(),
                },
                process_configs: Config::default_process_config(),
                thread_configs: Config::default_thread_config(),
            })
            .unwrap(),
        ));

        let (process_id, process) = fork_process_for_test();

        let result = set_process_state(sched_ctx.clone(), process_id, ProcessState::Normal);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert!(result.is_some());
        let join_handle = result.unwrap();

        tokio::time::sleep(Duration::from_millis(1)).await;

        assert!(!join_handle.is_finished());

        drop(process);

        // The remove_process() is executed. Otherwise this test times out.
        let _ = join_handle.await;
    }

    // pidfd_open(2) is not supported on qemu-user which CQ uses to run tests for non-x86_64
    // boards.
    #[cfg(target_arch = "x86_64")]
    #[tokio::test]
    async fn test_create_async_pidfd() {
        assert!(create_async_pidfd(std::process::id()).is_ok());

        let (process_id, process) = fork_process_for_test();
        assert!(create_async_pidfd(process_id).is_ok());

        drop(process);
        assert_eq!(
            create_async_pidfd(process_id).err().unwrap().raw_os_error(),
            Some(libc::ESRCH)
        );

        assert_eq!(
            create_async_pidfd(0).err().unwrap().raw_os_error(),
            Some(libc::EINVAL)
        );
    }
}
