// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::LogFile;
use std::ffi::c_char;

#[cfg(feature = "chromeos")]
use libchromeos::syslog;

#[cxx::bridge(namespace = "gravedigger::rust")]
pub mod ffi {
    #[derive(Debug)]
    pub enum SeekLocation {
        Begin,
        BeforeEnd,
        End,
    }

    extern "Rust" {
        type LogFile;
        type CreateResult;

        fn try_init(ident: &str) -> bool;

        fn log_file_exists(name: &str) -> bool;
        fn file_path_exists(path: &str) -> bool;

        fn new_log_file(name: &str) -> Box<LogFile>;
        fn new_log_file_from_path(path: &str) -> Box<CreateResult>;
        fn result_to_logfile(result: Box<CreateResult>) -> Box<LogFile>;

        fn get_inode(self: &LogFile) -> u64;
        fn get_file_length(self: &LogFile) -> i64;
        fn read_to_slice(log_file: &mut Box<LogFile>, buf: &mut [u8]) -> i64;
        unsafe fn read_to_char_ptr(
            log_file: &mut Box<LogFile>,
            data: *mut c_char,
            size: i64,
        ) -> i64;
        fn seek(self: &mut LogFile, location: SeekLocation) -> bool;

        fn has_value(self: &CreateResult) -> bool;
    }
}

/// Thin wrapper struct to communicate errors to the C++ side.
pub struct CreateResult {
    file: Option<LogFile>,
}

impl CreateResult {
    pub fn has_value(&self) -> bool {
        self.file.is_some()
    }
}

#[cfg(feature = "chromeos")]
fn try_init(ident: &str) -> bool {
    syslog::init(ident.to_string(), true).is_ok()
}

#[cfg(not(feature = "chromeos"))]
fn try_init(ident: &str) -> bool {
    !ident.is_empty()
}

/// Check if a logfile exists in the expected directory for SOUL.
fn log_file_exists(name: &str) -> bool {
    LogFile::exists(name)
}

/// CHeck if a file exists at the provided `path`.
fn file_path_exists(path: &str) -> bool {
    LogFile::path_exists(path)
}

/// Create a new logfile from the file that exists at `path`.
///
/// Panics if the file doesn't exist or can't be read.
fn new_log_file_from_path(path: &str) -> Box<CreateResult> {
    match LogFile::from_path(path) {
        Ok(log_file) => Box::new(CreateResult {
            file: Some(log_file),
        }),
        Err(err) => {
            log::error!("Couldn't create logfile because: {err}");
            Box::new(CreateResult { file: None })
        }
    }
}

/// Convert the initial `CreateResult` into a `LogFile`.
///
/// Panics if `result` doesn't hold a `LogFile`. Check this before with
/// `result.has_value()`.
fn result_to_logfile(result: Box<CreateResult>) -> Box<LogFile> {
    match result.file {
        Some(file) => Box::new(file),
        None => panic!("log file must have been created successfully before"),
    }
}

/// Create a new virtual logfile.
fn new_log_file(name: &str) -> Box<LogFile> {
    let log_file = LogFile::new(name).expect("Couldn't create LogFile object");

    Box::new(log_file)
}

/// Read up to `size` bytes into `data` from the `log_file`.
///
/// Returns a positive number if bytes have been read or a negative error code
/// if something went wrong.
///
/// `size` must not be negative and `data` must not be null.
/// Will do an internal conversion from c_char to u8.
fn read_to_char_ptr(log_file: &mut Box<LogFile>, data: *mut c_char, size: i64) -> i64 {
    if size < 0 {
        log::warn!("Reading negative {size} is not supported");
        return -1;
    } else if size == 0 {
        log::debug!("Requested to read 0 bytes. Doing nothing");
        return 0;
    }

    if data.is_null() {
        log::error!("Passed a nullptr as data");
        return -1;
    }

    let len = size as usize;

    let slice = unsafe { std::slice::from_raw_parts_mut(data.cast::<u8>(), len) };

    read_to_slice(log_file, slice)
}

/// Read bytes from `log_file` until `buf` is full.
///
/// Returns a positive number if bytes have been read or a negative error code
/// if something went wrong.
fn read_to_slice(log_file: &mut Box<LogFile>, buf: &mut [u8]) -> i64 {
    if buf.len() == 0 {
        log::debug!("Requested to read 0 bytes. Doing nothing");
        return 0;
    }

    match log_file.read(buf) {
        Ok(size) => return size,
        Err(_err) => return -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::TestLogFile;

    fn empty_log_file() -> (TestLogFile, Box<LogFile>) {
        valid_log_file("")
    }

    fn valid_log_file(data: &str) -> (TestLogFile, Box<LogFile>) {
        let test_file = TestLogFile::with_content(data);
        let log_file = test_file.as_logfile();
        (test_file, Box::new(log_file))
    }

    #[test]
    #[should_panic(expected = "No such file")]
    fn die_on_non_existing_file() {
        new_log_file("filename doesn't exist");
    }

    #[test]
    #[should_panic(expected = "Couldn't create LogFile")]
    fn die_on_path_instead_of_name() {
        new_log_file("Relative/Path");
    }

    #[test]
    fn read_negative_size() {
        let (_test_file, mut logfile) = empty_log_file();
        assert_eq!(read_to_char_ptr(&mut logfile, std::ptr::null_mut(), -1), -1);
    }

    #[test]
    fn read_zero_bytes() {
        let (_test_file, mut logfile) = empty_log_file();
        assert_eq!(read_to_char_ptr(&mut logfile, std::ptr::null_mut(), 0), 0);
    }

    #[test]
    fn read_into_nullptr() {
        let (_test_file, mut logfile) = empty_log_file();
        assert_eq!(read_to_char_ptr(&mut logfile, std::ptr::null_mut(), 1), -1);
    }

    #[test]
    fn read_one_byte() {
        let (_test_file, mut logfile) = valid_log_file("foo");
        let mut buf: c_char = 0;
        assert_eq!(read_to_char_ptr(&mut logfile, &mut buf, 1), 1);
        assert_eq!(buf, 'f' as c_char);
    }

    #[test]
    fn read_to_slice_vec() {
        let (_test_file, mut logfile) = valid_log_file("foo");
        let mut buf = vec![0; 3];
        assert_eq!(read_to_slice(&mut logfile, &mut buf), 3);
        assert_eq!(buf, b"foo");
    }

    #[test]
    fn read_to_slice_empty() {
        let (_test_file, mut logfile) = valid_log_file("foo");
        let mut buf = vec![];
        assert_eq!(read_to_slice(&mut logfile, &mut buf), 0);
    }

    #[test]
    fn log_file_exists_test() {
        assert!(log_file_exists("test_log_file"));
        assert!(!log_file_exists("filename doesn't exist"));
    }

    #[test]
    fn file_paths_exist() {
        assert!(file_path_exists(
            format!("{}/tests/test_log_file", env!("CARGO_MANIFEST_DIR")).as_str()
        ));
        assert_eq!(file_path_exists("relative/path"), false);
    }
}
