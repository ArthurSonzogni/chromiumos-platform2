// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod restorable;
pub mod simple;

use crate::ProcessId;
use crate::ProcessState;
use crate::ThreadId;
use crate::ThreadState;

pub trait ProcessContext {
    type TM<'a>: ThreadMap
    where
        Self: 'a;
    fn state(&self) -> ProcessState;
    fn thread_map(&mut self) -> Self::TM<'_>;
}

pub trait ProcessMap {
    type P<'a>: ProcessContext
    where
        Self: 'a;
    /// Insert a new process or update a process if exists.
    ///
    /// Returns [None] if inserting.
    ///
    /// Returns [ProcessContext] if the process exists and is updated.
    fn insert_or_update(
        &mut self,
        process_id: ProcessId,
        timestamp: u64,
        state: ProcessState,
    ) -> Option<Self::P<'_>>;
    fn get_process(&mut self, process_id: ProcessId) -> Option<Self::P<'_>>;
    /// Remove a process.
    ///
    /// `timestamp` is used to identify the process with `process_id` if it is `Option::Some`.
    /// Otherwise this does not check the stored timestamp in the map.
    fn remove_process(&mut self, process_id: ProcessId, timestamp: Option<u64>);
    /// Reduce storage size by compacting holes left by deleted processes and threads.
    ///
    /// NOTE: compact() should be called on every process/thread context update. It still works
    /// without compact()ing, but next compact() will take longer time for accumulating removed
    /// contexts which will cause inconsistent latency of the process/thread context update latency
    /// and performance degradation. [The Tail at Scale](https://research.google/pubs/pub40801/).
    fn compact(&mut self);
}

pub trait ThreadMap {
    /// Insert a new thread or update a thread if exist.
    ///
    /// Before inserting a new thread GC threads in the map. This is to avoid useless memory
    /// consumption of from dead threads for the case a process spawns many short-term threads while
    /// the process state keeps the same. The `fn_is_thread_alive` is used to check whether the
    /// thread is alive or not.
    fn insert_or_update<F>(
        &mut self,
        thread_id: ThreadId,
        timestamp: u64,
        state: ThreadState,
        fn_is_thread_alive: F,
    ) where
        F: FnMut(&ThreadId) -> bool;
    fn retain_threads<F>(&mut self, f: F)
    where
        F: FnMut(&ThreadId, &ThreadEntry) -> bool;
    fn remove_thread(&mut self, thread_id: ThreadId);
}

pub struct ThreadEntry {
    pub timestamp: u64,
    pub state: ThreadState,
}
