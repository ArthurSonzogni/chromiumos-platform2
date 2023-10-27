// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::Read;
use std::sync::mpsc::channel;
use std::sync::Arc;
use std::sync::Barrier;
use std::thread::JoinHandle;

pub(crate) use crate::sched_attr::assert_sched_attr;
use crate::CgroupContext;
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
