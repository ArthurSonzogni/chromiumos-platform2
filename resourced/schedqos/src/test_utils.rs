// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::Read;
use std::sync::mpsc::channel;
use std::sync::Arc;
use std::sync::Barrier;
use std::thread::sleep;
use std::thread::JoinHandle;
use std::time::Duration;

use crate::proc;
pub(crate) use crate::sched_attr::assert_sched_attr;
use crate::CgroupContext;
use crate::ProcessId;
use crate::ThreadId;

#[derive(Debug)]
pub struct FakeCgroupFiles {
    pub cpu_normal: File,
    pub cpu_background: File,
    pub cpuset_all: File,
    pub cpuset_efficient: File,
}

pub fn create_fake_cgroup_context_pair() -> (CgroupContext, FakeCgroupFiles) {
    let cpu_normal = tempfile::NamedTempFile::new().unwrap();
    let cpu_background = tempfile::NamedTempFile::new().unwrap();
    let cpuset_all = tempfile::NamedTempFile::new().unwrap();
    let cpuset_efficient = tempfile::NamedTempFile::new().unwrap();
    (
        CgroupContext {
            cpu_normal: cpu_normal.reopen().unwrap(),
            cpu_background: cpu_background.reopen().unwrap(),
            cpuset_all: cpuset_all.reopen().unwrap(),
            cpuset_efficient: cpuset_efficient.reopen().unwrap(),
        },
        FakeCgroupFiles {
            cpu_normal: cpu_normal.reopen().unwrap(),
            cpu_background: cpu_background.reopen().unwrap(),
            cpuset_all: cpuset_all.reopen().unwrap(),
            cpuset_efficient: cpuset_efficient.reopen().unwrap(),
        },
    )
}

pub fn read_number(file: &mut File) -> Option<u32> {
    let mut buf = [0; 1024];
    let n = file.read(buf.as_mut_slice()).unwrap();
    if n == 0 {
        None
    } else {
        Some(
            String::from_utf8(buf[..n].to_vec())
                .unwrap()
                .parse::<u32>()
                .unwrap(),
        )
    }
}

pub(crate) fn get_current_thread_id() -> ThreadId {
    ThreadId(unsafe { libc::gettid() } as u32)
}

pub struct ThreadForTest {
    join_handle: Option<JoinHandle<()>>,
    barrier: Arc<Barrier>,
}

impl Drop for ThreadForTest {
    fn drop(&mut self) {
        self.barrier.wait();
        self.join_handle.take().unwrap().join().unwrap();
    }
}

pub(crate) fn spawn_thread_for_test() -> (ThreadId, ThreadForTest) {
    let (sender, receiver) = channel();
    let barrier = Arc::new(Barrier::new(2));
    let barrier_on_thread = barrier.clone();
    let join_handle = std::thread::spawn(move || {
        sender.send(get_current_thread_id()).unwrap();
        barrier_on_thread.wait();
    });
    let thread_id = receiver.recv().unwrap();
    (
        thread_id,
        ThreadForTest {
            join_handle: Some(join_handle),
            barrier,
        },
    )
}

/// [std::thread::JoinHandle] does not guarantee that the thread is removed from
/// procfs.
///
/// Poll the procfs file until the files for the thread is removed. If the files
/// are not removed, this returns [false].
pub(crate) fn wait_for_thread_removed(process_id: ProcessId, thread_id: ThreadId) -> bool {
    for _ in 0..100 {
        if matches!(
            proc::load_thread_timestamp(process_id, thread_id),
            Err(proc::Error::NotFound)
        ) {
            return true;
        }
        sleep(Duration::from_millis(1));
    }
    false
}

pub struct ProcessForTest {
    process_id: ProcessId,
}

impl Drop for ProcessForTest {
    fn drop(&mut self) {
        let process_id = self.process_id.0 as libc::pid_t;
        unsafe {
            libc::kill(process_id, libc::SIGKILL);
            libc::waitpid(process_id, std::ptr::null_mut(), 0);
        }
    }
}

pub(crate) fn fork_process_for_test() -> (ProcessId, ThreadId, ProcessForTest) {
    let child_process_id = unsafe { libc::fork() };
    if child_process_id == 0 {
        loop {
            std::thread::sleep(Duration::from_secs(1));
        }
    }
    assert!(child_process_id > 0);
    let child_process_id = ProcessId(child_process_id as u32);
    (
        child_process_id,
        ThreadId(child_process_id.0),
        ProcessForTest {
            process_id: child_process_id,
        },
    )
}
