// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;
use std::fs::File;
use std::io;
use std::io::Read;
use std::path::Path;

use crate::ProcessId;
use crate::ThreadId;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    NotFound,
    FormatCorrupt,
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => e.fmt(f),
            Self::NotFound => f.write_str("process/thread not found"),
            Self::FormatCorrupt => f.write_str("stat file is corrupt"),
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

pub(crate) fn load_process_timestamp(process_id: ProcessId) -> Result<u64> {
    load_starttime(Path::new(&format!("/proc/{}/stat", process_id.0)))
}

pub(crate) fn load_thread_timestamp(process_id: ProcessId, thread_id: ThreadId) -> Result<u64> {
    load_starttime(Path::new(&format!(
        "/proc/{}/task/{}/stat",
        process_id.0, thread_id.0
    )))
}

fn load_starttime(path: &Path) -> Result<u64> {
    let mut stat_file = match File::open(path) {
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Err(Error::NotFound),
        other => other?,
    };
    // starttime is the 22th column in /proc/pid/stat. Each numeric column in /proc/pid/stat has at
    // most 21 bytes. (1 byte for sign + 19 bytes for u64 + 1 byte space). The 2nd column (comm) is
    // at most 67 bytes including the wrapping parenthesis (proc_task_name() of kernel uses 64 bytes
    // buffer `tcomm`). 512 bytes is enough to hold the 22 columns (i.e. 512 >= 21 * 21 + 67 = 508).
    let mut buf = [0; 512];
    let n = stat_file.read(&mut buf)?;

    // Since threads can set comm by writing to /proc/self/task/tid/comm, it can contain any byte
    // sequence. This means we need to exclusively look at kernel controlled bytes to determine
    // where comm ends. To do this, we can scan backwards to look for the closing parentheses
    // emitted by the kernel.
    // The longest possible tail offset of comm is 88 bytes (= 21 bytes for pid column + 1 byte for
    // space + 66 bytes for comm column). This works even if the stat file size is less than 88
    // bytes because the space after the stat file content are zeroed.
    let mut i_comm_tail = None;
    for (i, c) in buf[..88].iter().enumerate().rev() {
        if *c == b')' {
            i_comm_tail = Some(i);
            break;
        }
    }
    let Some(i_comm_tail) = i_comm_tail else {
        return Err(Error::FormatCorrupt);
    };

    let mut prev_space = i_comm_tail + 1;
    let mut starttime = None;
    // `pid` and `comm` columns are consumed.
    let mut column_idx = 2;
    for (i, c) in buf[..n].iter().enumerate().skip(prev_space + 1) {
        if *c == b' ' {
            if column_idx == 21 {
                // starttime is at 22th column.
                starttime = Some(prev_space + 1..i);
                break;
            }
            prev_space = i;
            column_idx += 1;
        }
    }
    let Some(starttime) = starttime else {
        return Err(Error::FormatCorrupt);
    };
    let starttime = std::str::from_utf8(&buf[starttime]).map_err(|_| Error::FormatCorrupt)?;
    let starttime = starttime.parse().map_err(|_| Error::FormatCorrupt)?;

    Ok(starttime)
}

pub struct ThreadChecker {
    prefix: String,
}

impl ThreadChecker {
    pub(crate) fn new(process_id: ProcessId) -> Self {
        // "/proc/" (6 bytes) + pid (at most 10 bytes) "/task/" (6 bytes) + tid (at most 10 bytes).
        let mut prefix = String::with_capacity(32);
        prefix.push_str("/proc/");
        prefix.push_str(&process_id.0.to_string());
        prefix.push_str("/task/");
        Self { prefix }
    }

    pub(crate) fn thread_exists(&mut self, thread_id: ThreadId) -> bool {
        let original_len = self.prefix.len();
        self.prefix.push_str(&thread_id.0.to_string());
        let result = Path::new(&self.prefix).exists();
        self.prefix.truncate(original_len);
        result
    }
}

#[cfg(test)]
mod tests {
    use std::io::Write;

    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_load_process_timestamp() {
        let process_id = ProcessId(std::process::id());
        assert!(load_process_timestamp(process_id).unwrap() > 0);

        let (process_id, _, _process) = fork_process_for_test();
        assert!(load_process_timestamp(process_id).unwrap() > 0);
    }

    #[test]
    fn test_load_process_timestamp_not_found() {
        let (process_id, _, process) = fork_process_for_test();
        drop(process);
        assert!(matches!(
            load_process_timestamp(process_id).err().unwrap(),
            Error::NotFound
        ));
    }

    #[test]
    fn test_load_thread_timestamp() {
        let process_id = ProcessId(std::process::id());
        let thread_id = get_current_thread_id();
        assert!(load_thread_timestamp(process_id, thread_id).unwrap() > 0);

        let (thread_id, _thread) = spawn_thread_for_test();
        assert!(load_thread_timestamp(process_id, thread_id).unwrap() > 0);

        let (process_id, thread_id, _process) = fork_process_for_test();
        assert!(load_thread_timestamp(process_id, thread_id).unwrap() > 0);
    }

    #[test]
    fn test_load_thread_timestamp_not_found() {
        let process_id = ProcessId(std::process::id());
        let thread_id = get_current_thread_id();
        let (child_process_id, child_thread_id, _process) = fork_process_for_test();

        assert!(matches!(
            load_thread_timestamp(process_id, child_thread_id)
                .err()
                .unwrap(),
            Error::NotFound
        ));
        assert!(matches!(
            load_thread_timestamp(child_process_id, thread_id)
                .err()
                .unwrap(),
            Error::NotFound
        ));
    }

    #[test]
    fn test_load_starttime() {
        for (stat_file_content, starttime) in [
            (
                "9345 (resourced) S 1 9344 9344 0 -1 1077936384 599 0 0 0 2851 2468 0 0 20 0 1 0 \
            41329081 19865600 2719 18446744073709551615 101386084483072 101386086716560 \
            140736188509360 0 0 0 0 4096 1088 1 0 0 17 0 0 0 0 0 0 101386086981672 101386086982232 \
            101386110365696 140736188513963 140736188513982 140736188513982 140736188514277 0",
                41329081,
            ),
            // Shortest stat file.
            ("1 (a) 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 ", 2),
            // Longest pid + comm stat file.
            (
                "1234567890123456789 \
            (1234567890123456789012345678901234567890123456789012345678901234) 3 4 5 6 7 8 9 0 1 2 \
            3 4 5 6 7 8 9 0 1 123 ",
                123,
            ),
            // comm contains spaces and parenthesis.
            (
                "1 ( a ( b ) c ) ) 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 456 ",
                456,
            ),
        ] {
            let mut file = tempfile::NamedTempFile::new().unwrap();
            file.write_all(stat_file_content.as_bytes()).unwrap();
            assert_eq!(
                load_starttime(file.path()).unwrap(),
                starttime,
                "{}",
                stat_file_content
            );
        }
    }

    #[test]
    fn test_load_starttime_corrupt_format() {
        for stat_file_content in [
            // no comm parenthesis.
            "1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2",
            // starttime is not a number.
            "1 (a) 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 a ",
            // no 22th space
            "1 (a) 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2",
        ] {
            let mut file = tempfile::NamedTempFile::new().unwrap();
            file.write_all(stat_file_content.as_bytes()).unwrap();
            assert!(
                matches!(
                    load_starttime(file.path()).err().unwrap(),
                    Error::FormatCorrupt
                ),
                "{}",
                stat_file_content
            );
        }

        let mut file = tempfile::NamedTempFile::new().unwrap();
        file.write_all(b"1 (a) 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2")
            .unwrap();
        // non utf-8
        file.write_all(&[255, b' ']).unwrap();
        assert!(matches!(
            load_starttime(file.path()).err().unwrap(),
            Error::FormatCorrupt
        ));
    }

    #[test]
    fn test_thread_exists() {
        let process_id = ProcessId(std::process::id());
        let mut checker = ThreadChecker::new(process_id);
        let (thread_id, thread) = spawn_thread_for_test();
        let (another_process_id, another_thread_id, process) = fork_process_for_test();
        let mut another_checker = ThreadChecker::new(another_process_id);

        assert!(checker.thread_exists(thread_id));
        assert!(!checker.thread_exists(another_thread_id));
        assert!(another_checker.thread_exists(another_thread_id));
        assert!(!another_checker.thread_exists(thread_id));
        drop(thread);
        wait_for_thread_removed(process_id, thread_id);
        assert!(!checker.thread_exists(thread_id));
        drop(process);
        assert!(!another_checker.thread_exists(another_thread_id));
    }
}
