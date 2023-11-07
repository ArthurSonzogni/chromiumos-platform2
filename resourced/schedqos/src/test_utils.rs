// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::Read;
use std::os::fd::OwnedFd;
use std::os::unix::net::UnixDatagram;
use std::sync::mpsc::channel;
use std::sync::Arc;
use std::sync::Barrier;
use std::thread::sleep;
use std::thread::JoinHandle;
use std::time::Duration;

use crate::proc::ThreadChecker;
pub(crate) use crate::sched_attr::assert_sched_attr;
pub(crate) use crate::sched_attr::SchedAttrChecker;
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

fn create_fake_file_pair() -> (File, File) {
    let (s1, s2) = UnixDatagram::pair().unwrap();
    s1.set_nonblocking(true).unwrap();
    s2.set_nonblocking(true).unwrap();
    let fd1: OwnedFd = s1.into();
    let fd2: OwnedFd = s2.into();
    (fd1.into(), fd2.into())
}

/// Create fake cgroup files backed by unix datagram sockets.
///
/// unix datagram socket is useful because there is no delimiters between process/thread ids.
///
/// [FakeCgroupFiles] must be retained while [CgroupContext] is used. Otherwise writes fail as
/// `ECONNREFUSED`.
pub fn create_fake_cgroup_context_pair() -> (CgroupContext, FakeCgroupFiles) {
    let cpu_normal = create_fake_file_pair();
    let cpu_background = create_fake_file_pair();
    let cpuset_all = create_fake_file_pair();
    let cpuset_efficient = create_fake_file_pair();
    (
        CgroupContext {
            cpu_normal: cpu_normal.0,
            cpu_background: cpu_background.0,
            cpuset_all: cpuset_all.0,
            cpuset_efficient: cpuset_efficient.0,
        },
        FakeCgroupFiles {
            cpu_normal: cpu_normal.1,
            cpu_background: cpu_background.1,
            cpuset_all: cpuset_all.1,
            cpuset_efficient: cpuset_efficient.1,
        },
    )
}

pub fn read_number(file: &mut File) -> Option<u32> {
    let mut buf = [0; 1024];
    match file.read(buf.as_mut_slice()) {
        Ok(0) => None,
        Ok(n) => Some(
            String::from_utf8(buf[..n].to_vec())
                .unwrap()
                .parse::<u32>()
                .unwrap(),
        ),
        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
        Err(e) => panic!("failed to read file: {:#}", e),
    }
}

pub struct CgroupFileIterator<'a>(&'a mut File);

impl Iterator for CgroupFileIterator<'_> {
    type Item = u32;

    fn next(&mut self) -> Option<Self::Item> {
        read_number(self.0)
    }
}

pub fn read_numbers(file: &mut File) -> CgroupFileIterator {
    CgroupFileIterator(file)
}

pub fn drain_file(file: &mut File) {
    while read_number(file).is_some() {}
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
    let mut checker = ThreadChecker::new(process_id);
    for _ in 0..100 {
        if !checker.thread_exists(thread_id) {
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
