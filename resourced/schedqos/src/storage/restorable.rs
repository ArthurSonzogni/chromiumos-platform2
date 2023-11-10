// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::hash_map::OccupiedEntry;
use std::collections::HashMap;
use std::fmt::Display;
use std::fs::File;
use std::fs::OpenOptions;
use std::io;
use std::num::NonZeroUsize;
use std::path::Path;

use crate::mmap::Mmap;
use crate::proc::load_process_timestamp;
use crate::proc::load_tgid;
use crate::proc::load_thread_timestamp;
use crate::storage::ProcessContext;
use crate::storage::ProcessMap;
use crate::storage::ThreadEntry;
use crate::storage::ThreadMap;
use crate::ProcessId;
use crate::ProcessState;
use crate::ThreadId;
use crate::ThreadState;

const PAGE_SIZE: usize = 4096;

const CELL_SIZE: usize = 16;
const ID_OFFSET: usize = 0;
const STATE_OFFSET: usize = 4;
const TYPE_OFFSET: usize = 5;
const TIMESTAMP_OFFSET: usize = 8;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    MalformedFile,
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(e) => Some(e),
            Self::MalformedFile => None,
        }
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => f.write_fmt(format_args!("io: {e}")),
            Self::MalformedFile => f.write_str("file is malformed"),
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

pub type Result<T> = std::result::Result<T, Error>;

#[inline]
fn offset_to_cell_idx(offset: usize) -> usize {
    (offset / CELL_SIZE) - 1
}

#[inline]
fn parse_timestamp(memory: &[u8], offset: usize) -> u64 {
    let timestamp_offset = offset + TIMESTAMP_OFFSET;
    u64::from_ne_bytes(
        memory[timestamp_offset..timestamp_offset + 8]
            .try_into()
            .unwrap(),
    )
}

#[inline]
fn parse_is_process(memory: &[u8], offset: usize) -> bool {
    memory[offset + TYPE_OFFSET] != 0
}

#[inline]
fn parse_id(memory: &[u8], offset: usize) -> u32 {
    let id_offset = offset + ID_OFFSET;
    u32::from_ne_bytes(memory[id_offset..id_offset + 4].try_into().unwrap())
}

pub struct RestorableProcessEntry {
    cell: RestorableCell,
    thread_map: HashMap<ThreadId, RestorableThreadEntry>,
}

struct RestorableThreadEntry {
    cell: RestorableCell,
}

pub struct RestorableProcessContext<'a> {
    storage: &'a mut RestorableStateStorage,
    process_id: ProcessId,
    entry: OccupiedEntry<'a, ProcessId, RestorableProcessEntry>,
}

#[cfg(test)]
impl RestorableProcessContext<'_> {
    fn timestamp(&self) -> u64 {
        self.entry.get().cell.timestamp(self.storage)
    }
}

impl<'a> ProcessContext for RestorableProcessContext<'a> {
    type TM<'b> = RestorableThreadMap<'b>  where Self: 'b;
    fn state(&self) -> ProcessState {
        self.entry
            .get()
            .cell
            .state(self.storage)
            .try_into()
            .expect("invalid process state")
    }

    fn thread_map(&mut self) -> RestorableThreadMap {
        RestorableThreadMap {
            storage: self.storage,
            process_id: self.process_id,
            map: &mut self.entry.get_mut().thread_map,
        }
    }
}

pub struct RestorableProcessMap {
    storage: RestorableStateStorage,
    map: HashMap<ProcessId, RestorableProcessEntry>,
}

impl RestorableProcessMap {
    /// Creates an empty [RestorableProcessMap].
    pub fn new(path: &Path) -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(path)?;
        let size = NonZeroUsize::new(PAGE_SIZE).unwrap();

        Ok(Self {
            storage: RestorableStateStorage::new(file, size)?,
            map: HashMap::new(),
        })
    }

    /// Load the file and creates [RestorableProcessMap].
    pub fn load(path: &Path) -> Result<Self> {
        let file = OpenOptions::new().read(true).write(true).open(path)?;
        let mut size = file.metadata()?.len() as usize;
        if size % PAGE_SIZE != 0 {
            return Err(Error::MalformedFile);
        }
        if size < PAGE_SIZE {
            size = PAGE_SIZE;
        }
        let size = NonZeroUsize::new(size).unwrap();

        let mut storage = RestorableStateStorage::new(file, size)?;

        let n_cells = storage.n_cells();
        if (n_cells + 1) * CELL_SIZE > storage.memory.len() {
            return Err(Error::MalformedFile);
        }

        let mut map = HashMap::new();

        // Loads processes.
        for i in 0..n_cells {
            let offset = (i + 1) * CELL_SIZE;
            if !parse_is_process(&storage.memory, offset) {
                // Threads are loaded in next loop.
                continue;
            }
            // Validate state is valid.
            let _state: ProcessState = storage.memory[offset + STATE_OFFSET]
                .try_into()
                .map_err(|_| Error::MalformedFile)?;

            let process_id = ProcessId(parse_id(&storage.memory, offset));
            let is_valid_process = match load_process_timestamp(process_id) {
                Ok(timestamp) => timestamp == parse_timestamp(&storage.memory, offset),
                Err(_) => false,
            };
            if is_valid_process {
                let process = RestorableProcessEntry {
                    cell: RestorableCell { offset },
                    thread_map: HashMap::new(),
                };
                map.insert(process_id, process);
            } else {
                storage.freed_cells.push(offset);
            }
        }

        storage.process_ids.reserve(n_cells);

        // Loads threads and build process_ids list.
        for i in 0..n_cells {
            let offset = (i + 1) * CELL_SIZE;
            let id = parse_id(&storage.memory, offset);
            if parse_is_process(&storage.memory, offset) {
                // Skip processes.
                storage.process_ids.push(ProcessId(id));
                continue;
            }
            let thread_id = ThreadId(id);
            let Ok(process_id) = load_tgid(thread_id) else {
                // Add dummy process id. This value is not used.
                storage.process_ids.push(ProcessId(id));
                storage.freed_cells.push(offset);
                continue;
            };
            storage.process_ids.push(process_id);

            let Some(process) = map.get_mut(&process_id) else {
                storage.freed_cells.push(offset);
                continue;
            };

            // Validate state is valid.
            let _state: ThreadState = storage.memory[offset + STATE_OFFSET]
                .try_into()
                .map_err(|_| Error::MalformedFile)?;

            let is_valid_thread = match load_thread_timestamp(process_id, thread_id) {
                Ok(timestamp) => timestamp == parse_timestamp(&storage.memory, offset),
                Err(_) => false,
            };
            if is_valid_thread {
                let thread = RestorableThreadEntry {
                    cell: RestorableCell { offset },
                };
                process.thread_map.insert(thread_id, thread);
            } else {
                storage.freed_cells.push(offset);
            }
        }

        let mut process_map = RestorableProcessMap { storage, map };
        process_map.compact();

        Ok(process_map)
    }

    #[cfg(test)]
    pub fn n_cells(&self) -> usize {
        self.storage.n_cells()
    }

    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.map.len()
    }
}

impl ProcessMap for RestorableProcessMap {
    type P<'a> = RestorableProcessContext<'a>;

    fn insert_or_update(
        &mut self,
        process_id: ProcessId,
        timestamp: u64,
        state: ProcessState,
    ) -> Option<RestorableProcessContext> {
        match self.map.entry(process_id) {
            Entry::Occupied(mut entry) => {
                let process = entry.get_mut();
                process.cell.update_timestamp(&mut self.storage, timestamp);
                process.cell.update_state(&mut self.storage, state as u8);
                Some(RestorableProcessContext {
                    storage: &mut self.storage,
                    process_id,
                    entry,
                })
            }
            Entry::Vacant(entry) => {
                entry.insert(RestorableProcessEntry {
                    cell: RestorableCell::allocate(
                        &mut self.storage,
                        process_id,
                        true,
                        process_id.0,
                        timestamp,
                        state as u8,
                    ),
                    thread_map: HashMap::new(),
                });
                None
            }
        }
    }

    fn get_process(&mut self, process_id: ProcessId) -> Option<RestorableProcessContext> {
        match self.map.entry(process_id) {
            Entry::Occupied(entry) => Some(RestorableProcessContext {
                storage: &mut self.storage,
                process_id,
                entry,
            }),
            Entry::Vacant(_) => None,
        }
    }

    fn remove_process(&mut self, process_id: ProcessId, timestamp: Option<u64>) {
        if let Entry::Occupied(entry) = self.map.entry(process_id) {
            if timestamp.is_none()
                || entry.get().cell.timestamp(&self.storage) == timestamp.unwrap()
            {
                let process = entry.remove();
                process.thread_map.values().for_each(|thread| {
                    self.storage.free_cell(thread.cell.offset);
                });
                self.storage.free_cell(process.cell.offset);
            }
        }
    }

    fn compact(&mut self) {
        self.storage.freed_cells.sort_unstable();
        let mut n_cells = self.storage.n_cells();
        let mut i_head = 0;

        // n_cells must be greater than 0 while freed_cells is not fully consumed.
        // Check `n_cells > 0` in the loop instead of assert!() to avoid panic by malformed data.
        while i_head < self.storage.freed_cells.len() && n_cells > 0 {
            let Some(last_freed_cell_offset) = self.storage.freed_cells.last() else {
                break;
            };
            // Check that the entry is still valid before defragging it
            if *last_freed_cell_offset >= n_cells * CELL_SIZE {
                self.storage.freed_cells.pop();
            } else {
                let offset = self.storage.freed_cells[i_head];
                // The first entry is the header. The tail cell is at the (n_cells+1)th entry.
                let tail_cell_offset = n_cells * CELL_SIZE;
                let tail_is_process = parse_is_process(&self.storage.memory, tail_cell_offset);
                let tail_cell_pid = self.storage.process_ids[n_cells - 1];

                let Some(process) = self.map.get_mut(&tail_cell_pid) else {
                    // The tail cell is unexpectedly removed from the map.
                    break;
                };
                if tail_is_process {
                    process.cell.offset = offset;
                } else {
                    let thread_id = parse_id(&self.storage.memory, tail_cell_offset);
                    let thread_id = ThreadId(thread_id);
                    let Some(thread) = process.thread_map.get_mut(&thread_id) else {
                        // The tail cell is unexpectedly removed from the map.
                        break;
                    };
                    thread.cell.offset = offset;
                }
                self.storage
                    .memory
                    .copy_within(tail_cell_offset..tail_cell_offset + CELL_SIZE, offset);
                self.storage.process_ids[offset_to_cell_idx(offset)] = tail_cell_pid;

                i_head += 1;
            }
            n_cells -= 1;
        }

        self.storage.set_n_cells(n_cells as u64);
        self.storage.process_ids.truncate(n_cells);
        self.storage.freed_cells.drain(..i_head);

        // If there are more than 1 page unused, shrink the mmap.
        let memory_size_threshold = (n_cells + 1) * CELL_SIZE + PAGE_SIZE;
        if memory_size_threshold < self.storage.memory.len() {
            let n_pages = memory_size_threshold / PAGE_SIZE;
            let new_size = NonZeroUsize::new(n_pages * PAGE_SIZE).unwrap();
            // The file is expected to be on tmpfs. truncate(2) and mremap(2) must not fail.
            self.storage
                .memory
                .resize(new_size)
                .expect("failed to resize");
        }
    }
}

pub struct RestorableThreadMap<'a> {
    storage: &'a mut RestorableStateStorage,
    process_id: ProcessId,
    map: &'a mut HashMap<ThreadId, RestorableThreadEntry>,
}

impl RestorableThreadMap<'_> {
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.map.len()
    }
}

impl ThreadMap for RestorableThreadMap<'_> {
    fn insert_or_update<F>(
        &mut self,
        thread_id: ThreadId,
        timestamp: u64,
        state: ThreadState,
        mut fn_is_thread_alive: F,
    ) where
        F: FnMut(&ThreadId) -> bool,
    {
        if let Some(thread) = self.map.get_mut(&thread_id) {
            thread.cell.update_timestamp(self.storage, timestamp);
            thread.cell.update_state(self.storage, state as u8);
        } else {
            self.map.retain(|thread_id, thread| {
                let remain = fn_is_thread_alive(thread_id);
                if !remain {
                    self.storage.free_cell(thread.cell.offset);
                }
                remain
            });
            self.map.insert(
                thread_id,
                RestorableThreadEntry {
                    cell: RestorableCell::allocate(
                        self.storage,
                        self.process_id,
                        false,
                        thread_id.0,
                        timestamp,
                        state as u8,
                    ),
                },
            );
        }
    }

    fn retain_threads<F>(&mut self, mut f: F)
    where
        F: FnMut(&ThreadId, &ThreadEntry) -> bool,
    {
        self.map.retain(|thread_id, thread| {
            let remain = f(
                thread_id,
                &mut ThreadEntry {
                    timestamp: thread.cell.timestamp(self.storage),
                    state: thread
                        .cell
                        .state(self.storage)
                        .try_into()
                        .expect("invalid thread state"),
                },
            );
            if !remain {
                self.storage.free_cell(thread.cell.offset);
            }
            remain
        });
    }

    fn remove_thread(&mut self, thread_id: ThreadId) {
        if let Some(thread) = self.map.remove(&thread_id) {
            self.storage.free_cell(thread.cell.offset);
        }
    }
}

/// [RestorableStateStorage] stores each process/thread state in a file mmap(2)ed.
///
/// # File format
///
/// The file consists of an array whose entries are 16 bytes each. The first element is the header.
/// Each element after the header is a cell for process/thread entry.
///
/// ## Header
///
/// The first 8 bytes of the header are the total number of cells in the file in a native endian.
///
/// 9th ~ 16th bytes of the header are reserved.
///
/// ## Cell
///
/// The first 4 bytes of each cell are the process/thread id in a native endian.
///
/// 5th byte of each cell is the state of the process/thread.
///
/// 6th byte of each cell is whether the cell is a process or not. (1 = process, 0 = thread).
///
/// 7th and 8th bytes of each cell are reserved area.
///
/// 9th ~ 16th bytes of each cell is the starttime timestamp of the process/thread in a native
/// endian.
struct RestorableStateStorage {
    memory: Mmap,
    /// process_ids is used to find the [RestorableThreadEntry] of the tail cell in the hashmap
    /// on `compact()`.
    process_ids: Vec<ProcessId>,
    freed_cells: Vec<usize>,
}

impl RestorableStateStorage {
    fn new(file: File, size: NonZeroUsize) -> Result<Self> {
        let memory = Mmap::new(file, size)?;

        Ok(Self {
            memory,
            process_ids: Vec::new(),
            freed_cells: Vec::new(),
        })
    }

    fn n_cells(&self) -> usize {
        u64::from_ne_bytes(self.memory[0..8].try_into().unwrap()) as usize
    }

    fn set_n_cells(&mut self, n_cells: u64) {
        self.memory[0..8].copy_from_slice(&n_cells.to_ne_bytes());
    }

    fn allocate_cell(&mut self, process_id: ProcessId) -> usize {
        if let Some(offset) = self.freed_cells.pop() {
            self.process_ids[offset_to_cell_idx(offset)] = process_id;
            offset
        } else {
            let n_cells = self.n_cells() + 1;
            self.set_n_cells(n_cells as u64);
            let offset = n_cells * CELL_SIZE;

            if offset + CELL_SIZE > self.memory.len() {
                let new_size = NonZeroUsize::new(self.memory.len() + PAGE_SIZE).unwrap();
                // The file is expected to be on tmpfs. truncate(2) and mremap(2) must not fail.
                self.memory.resize(new_size).expect("failed to resize");
            }

            self.process_ids.push(process_id);

            offset
        }
    }

    fn free_cell(&mut self, offset: usize) {
        self.freed_cells.push(offset);
    }
}

struct RestorableCell {
    offset: usize,
}

impl RestorableCell {
    fn allocate(
        storage: &mut RestorableStateStorage,
        process_id: ProcessId,
        is_process: bool,
        id: u32,
        timestamp: u64,
        state: u8,
    ) -> RestorableCell {
        let offset = storage.allocate_cell(process_id);
        let cell = Self { offset };

        // Initializing the cell.
        let pid_offset = offset + ID_OFFSET;
        storage.memory[pid_offset..pid_offset + 4].copy_from_slice(&id.to_ne_bytes());
        cell.update_timestamp(storage, timestamp);
        cell.update_state(storage, state);
        storage.memory[offset + TYPE_OFFSET] = is_process as u8;

        cell
    }

    fn update_timestamp(&self, storage: &mut RestorableStateStorage, timestamp: u64) {
        let timestamp_offset = self.offset + TIMESTAMP_OFFSET;
        storage.memory[timestamp_offset..timestamp_offset + 8]
            .copy_from_slice(&timestamp.to_ne_bytes());
    }

    fn update_state(&self, storage: &mut RestorableStateStorage, state: u8) {
        let state_offset = self.offset + STATE_OFFSET;
        storage.memory[state_offset] = state;
    }

    fn timestamp(&self, storage: &RestorableStateStorage) -> u64 {
        parse_timestamp(&storage.memory, self.offset)
    }

    fn state(&self, storage: &RestorableStateStorage) -> u8 {
        let state_offset = self.offset + STATE_OFFSET;
        storage.memory[state_offset]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::fork_process_for_test;
    use crate::test_utils::spawn_thread_for_test;
    use crate::test_utils::wait_for_thread_removed;

    #[test]
    fn test_process_insert_or_update() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        assert!(map
            .insert_or_update(ProcessId(1000), 12345, ProcessState::Normal)
            .is_none());
        assert!(map
            .insert_or_update(ProcessId(1001), 23456, ProcessState::Background)
            .is_none());

        let process = map.get_process(ProcessId(1000)).unwrap();
        assert_eq!(process.timestamp(), 12345);
        assert_eq!(process.state(), ProcessState::Normal);
        let process = map.get_process(ProcessId(1001)).unwrap();
        assert_eq!(process.timestamp(), 23456);
        assert_eq!(process.state(), ProcessState::Background);

        assert!(map
            .insert_or_update(ProcessId(1000), 12345, ProcessState::Background)
            .is_some());
        assert!(map
            .insert_or_update(ProcessId(1001), 65432, ProcessState::Normal)
            .is_some());

        let process = map.get_process(ProcessId(1000)).unwrap();
        assert_eq!(process.timestamp(), 12345);
        assert_eq!(process.state(), ProcessState::Background);
        let process = map.get_process(ProcessId(1001)).unwrap();
        assert_eq!(process.timestamp(), 65432);
        assert_eq!(process.state(), ProcessState::Normal);

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
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

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
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();
        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Normal);
        let mut process = map.get_process(ProcessId(1000)).unwrap();
        let mut thread_map = process.thread_map();

        // Insert
        thread_map.insert_or_update(ThreadId(1000), 12345, ThreadState::Balanced, |_| true);
        thread_map.insert_or_update(ThreadId(1001), 23456, ThreadState::Urgent, |_| true);
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            12345
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Balanced as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1001))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            23456
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1001))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Urgent as u8
        );

        // Update
        thread_map.insert_or_update(ThreadId(1000), 54321, ThreadState::Utility, |_| true);
        thread_map.insert_or_update(
            ThreadId(1001),
            65432,
            ThreadState::Eco,
            |_| false, // Does not drain on update
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            54321
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Utility as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1001))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            65432
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1001))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Eco as u8
        );

        // Drain on inserting
        thread_map.insert_or_update(
            ThreadId(1002),
            34567,
            ThreadState::UrgentBursty,
            |thread_id| thread_id != &ThreadId(1000),
        );
        assert!(thread_map.map.get(&ThreadId(1000)).is_none());
        assert!(thread_map.map.get(&ThreadId(1001)).is_some());
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1002))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            34567
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1002))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::UrgentBursty as u8
        );
    }

    #[test]
    fn test_thread_remove() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();
        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Normal);
        let mut process = map.get_process(ProcessId(1000)).unwrap();
        let mut thread_map = process.thread_map();

        thread_map.insert_or_update(ThreadId(1000), 12345, ThreadState::Balanced, |_| true);
        thread_map.insert_or_update(ThreadId(1001), 23456, ThreadState::Urgent, |_| true);

        thread_map.remove_thread(ThreadId(1000));
        assert!(thread_map.map.get(&ThreadId(1000)).is_none());
        assert!(thread_map.map.get(&ThreadId(1001)).is_some());
    }

    #[test]
    fn test_thread_retain_threads() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();
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

    #[test]
    fn test_compact() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        map.insert_or_update(ProcessId(1000), 12345, ProcessState::Background);
        map.insert_or_update(ProcessId(1001), 23456, ProcessState::Normal);
        map.insert_or_update(ProcessId(1002), 34567, ProcessState::Normal);

        let mut process = map.get_process(ProcessId(1000)).unwrap();
        process.thread_map().insert_or_update(
            ThreadId(1000),
            12345,
            ThreadState::UrgentBursty,
            |_| true,
        );
        process
            .thread_map()
            .insert_or_update(ThreadId(2000), 45678, ThreadState::Urgent, |_| true);

        let mut process = map.get_process(ProcessId(1001)).unwrap();
        process
            .thread_map()
            .insert_or_update(ThreadId(1001), 23456, ThreadState::Balanced, |_| true);
        process
            .thread_map()
            .insert_or_update(ThreadId(3000), 56789, ThreadState::Eco, |_| true);

        let mut process = map.get_process(ProcessId(1002)).unwrap();
        process
            .thread_map()
            .insert_or_update(ThreadId(1002), 34567, ThreadState::Utility, |_| true);
        // Remove the last cell.
        process.thread_map().insert_or_update(
            ThreadId(4000),
            67890,
            ThreadState::Background,
            |_| true,
        );
        assert_eq!(map.n_cells(), 9);

        // compact() to no deleted cells cause no error.
        map.compact();
        assert_eq!(map.n_cells(), 9);

        map.remove_process(ProcessId(1001), None);
        map.get_process(ProcessId(1000))
            .unwrap()
            .thread_map()
            .remove_thread(ThreadId(2000));
        map.get_process(ProcessId(1002))
            .unwrap()
            .thread_map()
            .remove_thread(ThreadId(4000));
        // Removing cells does not change n_cells() immediately.
        assert_eq!(map.n_cells(), 9);

        map.compact();
        assert_eq!(map.n_cells(), 4);

        let mut process = map.get_process(ProcessId(1000)).unwrap();
        assert_eq!(process.state(), ProcessState::Background);
        assert_eq!(process.timestamp(), 12345);
        assert_eq!(process.thread_map().len(), 1);
        let thread_map = process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::UrgentBursty as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1000))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            12345
        );
        let mut process = map.get_process(ProcessId(1002)).unwrap();
        assert_eq!(process.state(), ProcessState::Normal);
        assert_eq!(process.timestamp(), 34567);
        assert_eq!(process.thread_map().len(), 1);
        let thread_map = process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1002))
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Utility as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&ThreadId(1002))
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            34567
        );

        map.remove_process(ProcessId(1000), None);
        map.remove_process(ProcessId(1002), None);

        map.compact();
        assert_eq!(map.n_cells(), 0);
    }

    #[test]
    fn test_compact_empty() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        assert_eq!(map.n_cells(), 0);

        // compact() to empty storage cause no error.
        map.compact();
        assert_eq!(map.n_cells(), 0);
    }

    #[test]
    fn test_allocate_new_page() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        // The first entry is header.
        for i in 0..(PAGE_SIZE / CELL_SIZE) - 1 {
            map.insert_or_update(ProcessId(i as u32), i as u64, ProcessState::Normal);
        }
        assert_eq!(map.n_cells(), PAGE_SIZE / CELL_SIZE - 1);
        assert_eq!(file_path.metadata().unwrap().len(), PAGE_SIZE as u64);

        map.insert_or_update(
            ProcessId((PAGE_SIZE / CELL_SIZE) as u32 - 1),
            (PAGE_SIZE / CELL_SIZE) as u64 - 1,
            ProcessState::Normal,
        );
        assert_eq!(map.n_cells(), PAGE_SIZE / CELL_SIZE);
        // Allocate a new page.
        assert_eq!(file_path.metadata().unwrap().len(), 2 * PAGE_SIZE as u64);

        for i in 0..(PAGE_SIZE / CELL_SIZE) {
            let process = map.get_process(ProcessId(i as u32)).unwrap();
            assert_eq!(process.timestamp(), i as u64);
            assert_eq!(process.state(), ProcessState::Normal);
        }

        map.remove_process(ProcessId(0), None);
        map.compact();
        assert_eq!(map.n_cells(), PAGE_SIZE / CELL_SIZE - 1);
        // Does not free the last page.
        assert_eq!(file_path.metadata().unwrap().len(), 2 * PAGE_SIZE as u64);

        // Assert that content is not broken and clear the map.
        for i in 1..(PAGE_SIZE / CELL_SIZE) {
            let process = map.get_process(ProcessId(i as u32)).unwrap();
            assert_eq!(process.timestamp(), i as u64);
            assert_eq!(process.state(), ProcessState::Normal);

            map.remove_process(ProcessId(i as u32), None);
        }

        map.compact();
        assert_eq!(map.n_cells(), 0);
        // Free the last page.
        assert_eq!(file_path.metadata().unwrap().len(), PAGE_SIZE as u64);
    }

    #[test]
    fn test_load() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        let process_id = ProcessId(std::process::id());
        map.insert_or_update(
            process_id,
            load_process_timestamp(process_id).unwrap(),
            ProcessState::Normal,
        );
        let (thread_id, _thread) = spawn_thread_for_test();
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id,
                load_thread_timestamp(process_id, thread_id).unwrap(),
                ThreadState::Balanced,
                |_| true,
            );
        let (another_process_id, another_thread_id, _another_process) = fork_process_for_test();
        map.insert_or_update(
            another_process_id,
            load_process_timestamp(another_process_id).unwrap(),
            ProcessState::Background,
        );
        map.get_process(another_process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                another_thread_id,
                load_thread_timestamp(another_process_id, another_thread_id).unwrap(),
                ThreadState::Urgent,
                |_| true,
            );
        // Dropping map cause process/thread context drops. However the file content is not updated
        // without compact().
        drop(map);

        let mut map = RestorableProcessMap::load(&file_path).unwrap();

        assert_eq!(map.n_cells(), 4);
        assert_eq!(map.len(), 2);
        let mut process = map.get_process(process_id).unwrap();
        assert_eq!(process.state(), ProcessState::Normal);
        assert_eq!(
            process.timestamp(),
            load_process_timestamp(process_id).unwrap()
        );
        assert_eq!(process.thread_map().len(), 1);
        let thread_map = process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&thread_id)
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Balanced as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&thread_id)
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            load_thread_timestamp(process_id, thread_id).unwrap()
        );

        let mut another_process = map.get_process(another_process_id).unwrap();
        assert_eq!(another_process.state(), ProcessState::Background);
        assert_eq!(
            another_process.timestamp(),
            load_process_timestamp(another_process_id).unwrap()
        );
        assert_eq!(another_process.thread_map().len(), 1);
        let thread_map = another_process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&another_thread_id)
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Urgent as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&another_thread_id)
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            load_thread_timestamp(another_process_id, another_thread_id).unwrap()
        );
    }

    #[test]
    fn test_load_dead_processes_and_threads() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let mut map = RestorableProcessMap::new(&file_path).unwrap();

        let process_id = ProcessId(std::process::id());
        map.insert_or_update(
            process_id,
            load_process_timestamp(process_id).unwrap(),
            ProcessState::Normal,
        );
        let (thread_id1, dead_thread1) = spawn_thread_for_test();
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id1,
                load_thread_timestamp(process_id, thread_id1).unwrap(),
                ThreadState::Balanced,
                |_| true,
            );
        let (thread_id2, _thread2) = spawn_thread_for_test();
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id2,
                load_thread_timestamp(process_id, thread_id2).unwrap(),
                ThreadState::Urgent,
                |_| true,
            );
        let (thread_id3, _obsolete_thread3) = spawn_thread_for_test();
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id3,
                0, // obsolete timestamp.
                ThreadState::UrgentBursty,
                |_| true,
            );
        let (thread_id4, dead_thread4) = spawn_thread_for_test();
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id4,
                load_thread_timestamp(process_id, thread_id4).unwrap(),
                ThreadState::UrgentBursty,
                |_| true,
            );
        let (process_id1, thread_id5, dead_process1) = fork_process_for_test();
        map.insert_or_update(
            process_id1,
            load_process_timestamp(process_id1).unwrap(),
            ProcessState::Background,
        );
        map.get_process(process_id1)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id5,
                load_thread_timestamp(process_id1, thread_id5).unwrap(),
                ThreadState::Utility,
                |_| true,
            );
        let (process_id2, thread_id6, _process2) = fork_process_for_test();
        map.insert_or_update(
            process_id2,
            load_process_timestamp(process_id2).unwrap(),
            ProcessState::Background,
        );
        map.get_process(process_id2)
            .unwrap()
            .thread_map()
            .insert_or_update(
                thread_id6,
                load_thread_timestamp(process_id2, thread_id6).unwrap(),
                ThreadState::Background,
                |_| true,
            );

        // By removing the thread at second cell, the cell at the tail of the file pointing a thread
        // is moved to the second cell. I.e. the thread cell is located before the process cell.
        drop(dead_thread1);
        wait_for_thread_removed(process_id, thread_id1);
        map.get_process(process_id)
            .unwrap()
            .thread_map()
            .remove_thread(thread_id1);
        map.compact();

        drop(dead_thread4);
        wait_for_thread_removed(process_id, thread_id4);
        drop(dead_process1);

        let mut map = RestorableProcessMap::load(&file_path).unwrap();

        assert_eq!(map.n_cells(), 4);
        assert_eq!(map.len(), 2);
        let mut process = map.get_process(process_id).unwrap();
        assert_eq!(process.state(), ProcessState::Normal);
        assert_eq!(
            process.timestamp(),
            load_process_timestamp(process_id).unwrap()
        );
        assert_eq!(process.thread_map().len(), 1);
        let thread_map = process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&thread_id2)
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Urgent as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&thread_id2)
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            load_thread_timestamp(process_id, thread_id2).unwrap()
        );

        let mut process = map.get_process(process_id2).unwrap();
        assert_eq!(process.state(), ProcessState::Background);
        assert_eq!(
            process.timestamp(),
            load_process_timestamp(process_id2).unwrap()
        );
        assert_eq!(process.thread_map().len(), 1);
        let thread_map = process.thread_map();
        assert_eq!(
            thread_map
                .map
                .get(&thread_id6)
                .unwrap()
                .cell
                .state(thread_map.storage),
            ThreadState::Background as u8
        );
        assert_eq!(
            thread_map
                .map
                .get(&thread_id6)
                .unwrap()
                .cell
                .timestamp(thread_map.storage),
            load_thread_timestamp(process_id2, thread_id6).unwrap()
        );
    }

    #[test]
    fn test_load_empty() {
        let dir = tempfile::tempdir().unwrap();
        let file_path = dir.path().join("states");
        let map = RestorableProcessMap::new(&file_path).unwrap();

        assert_eq!(map.n_cells(), 0);

        // load() to empty storage cause no error.
        let map = RestorableProcessMap::load(&file_path).unwrap();
        assert_eq!(map.n_cells(), 0);
        assert!(map.map.is_empty());
    }
}
