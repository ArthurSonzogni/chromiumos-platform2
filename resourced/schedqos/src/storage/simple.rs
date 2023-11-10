// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::hash_map::OccupiedEntry;
use std::collections::HashMap;

use super::ProcessContext;
use crate::storage::ProcessMap;
use crate::storage::ThreadEntry;
use crate::storage::ThreadMap;
use crate::ProcessId;
use crate::ProcessState;
use crate::ThreadId;
use crate::ThreadState;

pub type SimpleProcessContext<'a> = OccupiedEntry<'a, ProcessId, SimpleProcessEntry>;
pub type SimpleProcessMap = HashMap<ProcessId, SimpleProcessEntry>;
pub type SimpleThreadMap<'a> = &'a mut HashMap<ThreadId, ThreadEntry>;

pub struct SimpleProcessEntry {
    timestamp: u64,
    state: ProcessState,
    thread_map: HashMap<ThreadId, ThreadEntry>,
}

impl<'a> ProcessContext for SimpleProcessContext<'a> {
    type TM<'b> = SimpleThreadMap<'b>  where Self: 'b;

    fn state(&self) -> ProcessState {
        self.get().state
    }

    fn thread_map(&mut self) -> SimpleThreadMap {
        &mut self.get_mut().thread_map
    }
}

impl ProcessMap for SimpleProcessMap {
    type P<'a> = SimpleProcessContext<'a>;

    fn insert_or_update(
        &mut self,
        process_id: ProcessId,
        timestamp: u64,
        state: ProcessState,
    ) -> Option<SimpleProcessContext> {
        match self.entry(process_id) {
            Entry::Occupied(mut entry) => {
                let process = entry.get_mut();
                process.timestamp = timestamp;
                process.state = state;
                Some(entry)
            }
            Entry::Vacant(entry) => {
                entry.insert(SimpleProcessEntry {
                    timestamp,
                    state,
                    thread_map: HashMap::new(),
                });
                None
            }
        }
    }

    fn get_process(&mut self, process_id: ProcessId) -> Option<SimpleProcessContext> {
        match self.entry(process_id) {
            Entry::Occupied(entry) => Some(entry),
            Entry::Vacant(_) => None,
        }
    }

    fn remove_process(&mut self, process_id: ProcessId, timestamp: Option<u64>) {
        if let Entry::Occupied(entry) = self.entry(process_id) {
            if timestamp.is_none() || entry.get().timestamp == timestamp.unwrap() {
                entry.remove();
            }
        }
    }

    fn compact(&mut self) {
        // No-op.
    }
}

impl ThreadMap for SimpleThreadMap<'_> {
    fn insert_or_update<F>(
        &mut self,
        thread_id: ThreadId,
        timestamp: u64,
        state: ThreadState,
        mut fn_is_thread_alive: F,
    ) where
        F: FnMut(&ThreadId) -> bool,
    {
        if let Some(thread) = self.get_mut(&thread_id) {
            thread.timestamp = timestamp;
            thread.state = state;
        } else {
            self.retain_threads(|tid, _| fn_is_thread_alive(tid));
            self.insert(thread_id, ThreadEntry { timestamp, state });
        }
    }

    fn retain_threads<F>(&mut self, mut f: F)
    where
        F: FnMut(&ThreadId, &ThreadEntry) -> bool,
    {
        self.retain(|tid, t| f(tid, t));
    }

    fn remove_thread(&mut self, thread_id: ThreadId) {
        self.remove(&thread_id);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_process_insert_or_update() {
        let mut map = SimpleProcessMap::new();

        assert!(map
            .insert_or_update(ProcessId(1000), 12345, ProcessState::Normal)
            .is_none());
        assert!(map
            .insert_or_update(ProcessId(1001), 23456, ProcessState::Background)
            .is_none());

        let process = map.get(&ProcessId(1000)).unwrap();
        assert_eq!(process.timestamp, 12345);
        assert_eq!(process.state, ProcessState::Normal);
        let process = map.get(&ProcessId(1001)).unwrap();
        assert_eq!(process.timestamp, 23456);
        assert_eq!(process.state, ProcessState::Background);

        assert!(map
            .insert_or_update(ProcessId(1000), 12345, ProcessState::Background)
            .is_some());
        assert!(map
            .insert_or_update(ProcessId(1001), 65432, ProcessState::Normal)
            .is_some());

        let process = map.get(&ProcessId(1000)).unwrap();
        assert_eq!(process.timestamp, 12345);
        assert_eq!(process.state, ProcessState::Background);
        let process = map.get(&ProcessId(1001)).unwrap();
        assert_eq!(process.timestamp, 65432);
        assert_eq!(process.state, ProcessState::Normal);

        assert_eq!(
            map.get_process(ProcessId(1000)).unwrap().state(),
            ProcessState::Background
        );
        assert_eq!(
            map.get_process(ProcessId(1001)).unwrap().state(),
            ProcessState::Normal
        );
        assert!(map.get_process(ProcessId(1002)).is_none());
    }

    #[test]
    fn test_process_remove() {
        let mut map = SimpleProcessMap::new();

        assert!(map
            .insert_or_update(ProcessId(1000), 12345, ProcessState::Normal)
            .is_none());
        assert!(map
            .insert_or_update(ProcessId(1001), 23456, ProcessState::Normal)
            .is_none());
        assert!(map
            .insert_or_update(ProcessId(1002), 34567, ProcessState::Normal)
            .is_none());

        map.remove_process(ProcessId(1000), None);
        assert!(map.get_process(ProcessId(1000)).is_none());
        map.remove_process(ProcessId(1001), Some(23456));
        assert!(map.get_process(ProcessId(1001)).is_none());
        map.remove_process(ProcessId(1002), Some(0));
        assert!(map.get_process(ProcessId(1002)).is_some());
    }

    #[test]
    fn test_thread_insert_or_update() {
        let mut map = SimpleProcessMap::new();
        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Normal);
        let mut process = map.get_process(ProcessId(1000)).unwrap();
        let mut thread_map = process.thread_map();

        // Insert
        thread_map.insert_or_update(ThreadId(1000), 12345, ThreadState::Balanced, |_| true);
        thread_map.insert_or_update(ThreadId(1001), 23456, ThreadState::Urgent, |_| true);
        assert_eq!(thread_map.get(&ThreadId(1000)).unwrap().timestamp, 12345);
        assert_eq!(
            thread_map.get(&ThreadId(1000)).unwrap().state,
            ThreadState::Balanced
        );
        assert_eq!(thread_map.get(&ThreadId(1001)).unwrap().timestamp, 23456);
        assert_eq!(
            thread_map.get(&ThreadId(1001)).unwrap().state,
            ThreadState::Urgent
        );

        // Update
        thread_map.insert_or_update(ThreadId(1000), 54321, ThreadState::Utility, |_| true);
        thread_map.insert_or_update(
            ThreadId(1001),
            65432,
            ThreadState::Eco,
            |_| false, // Does not drain on update
        );
        assert_eq!(thread_map.get(&ThreadId(1000)).unwrap().timestamp, 54321);
        assert_eq!(
            thread_map.get(&ThreadId(1000)).unwrap().state,
            ThreadState::Utility
        );
        assert_eq!(thread_map.get(&ThreadId(1001)).unwrap().timestamp, 65432);
        assert_eq!(
            thread_map.get(&ThreadId(1001)).unwrap().state,
            ThreadState::Eco
        );

        // Drain on inserting
        thread_map.insert_or_update(
            ThreadId(1002),
            34567,
            ThreadState::UrgentBursty,
            |thread_id| thread_id != &ThreadId(1000),
        );
        assert!(thread_map.get(&ThreadId(1000)).is_none());
        assert!(thread_map.get(&ThreadId(1001)).is_some());
        assert_eq!(thread_map.get(&ThreadId(1002)).unwrap().timestamp, 34567);
        assert_eq!(
            thread_map.get(&ThreadId(1002)).unwrap().state,
            ThreadState::UrgentBursty
        );
    }

    #[test]
    fn test_thread_remove() {
        let mut map = SimpleProcessMap::new();
        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Normal);
        let mut process = map.get_process(ProcessId(1000)).unwrap();
        let mut thread_map = process.thread_map();

        thread_map.insert_or_update(ThreadId(1000), 12345, ThreadState::Balanced, |_| true);
        thread_map.insert_or_update(ThreadId(1001), 23456, ThreadState::Urgent, |_| true);

        thread_map.remove_thread(ThreadId(1000));
        assert!(thread_map.get(&ThreadId(1000)).is_none());
        assert!(thread_map.get(&ThreadId(1001)).is_some());
    }

    #[test]
    fn test_thread_retain_threads() {
        let mut map = SimpleProcessMap::new();
        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Normal);
        let mut process = map.get_process(ProcessId(1000)).unwrap();
        let mut thread_map = process.thread_map();

        thread_map.insert_or_update(ThreadId(1000), 12345, ThreadState::Balanced, |_| true);
        thread_map.insert_or_update(ThreadId(1001), 23456, ThreadState::Urgent, |_| true);
        thread_map.insert_or_update(ThreadId(1002), 34567, ThreadState::UrgentBursty, |_| true);

        let mut threads = Vec::new();
        thread_map.retain_threads(|thread_id, thread| {
            threads.push((*thread_id, thread.state, thread.timestamp));
            thread_id != &ThreadId(1001)
        });
        assert_eq!(threads.len(), 3);
        assert!(threads.contains(&(ThreadId(1000), ThreadState::Balanced, 12345)));
        assert!(threads.contains(&(ThreadId(1001), ThreadState::Urgent, 23456)));
        assert!(threads.contains(&(ThreadId(1002), ThreadState::UrgentBursty, 34567)));
        assert_eq!(thread_map.len(), 2);

        let mut threads = Vec::new();
        thread_map.retain_threads(|thread_id, thread| {
            threads.push((*thread_id, thread.state, thread.timestamp));
            false
        });
        assert_eq!(threads.len(), 2);
        assert!(threads.contains(&(ThreadId(1000), ThreadState::Balanced, 12345)));
        assert!(threads.contains(&(ThreadId(1002), ThreadState::UrgentBursty, 34567)));
        assert_eq!(thread_map.len(), 0);
    }
}
