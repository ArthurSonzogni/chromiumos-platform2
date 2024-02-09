// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod ffi;

use std::fs::File;
use std::io::{Error, ErrorKind, Read, Result, Seek, SeekFrom};
use std::os::unix::fs::MetadataExt;
use std::path::Path;

use crate::ffi::ffi::SeekLocation;

/// Represents a (virtual) log file.
///
/// Applications shouldn't make assumptions about files stored in this structure
/// or where it's reading data from. The storage location is an implementation
/// detail and users of the API shouldn't try to read data themselves.
///
/// # Example
/// ```rust
/// let mut log_file = LogFile::new("test_log_file").expect("test log file exists");
/// let mut data = vec![0; log_file.get_file_length() as usize];
/// log_file.read(data.as_mut_slice());
/// println!("{data:?}");
/// ```
#[derive(Debug)]
pub struct LogFile {
    file: File,
}

/// Make sure the usize fits into an i64.
///
/// Will return i64::MAX if the value would be bigger.
///
/// ```rust
/// assert_eq!(capping_usize_to_i64(usize::MAX), i64::MAX);
/// assert_eq!(capping_usize_to_i64(1), 1);
/// ```
fn capping_usize_to_i64(val: usize) -> i64 {
    if usize::BITS <= u64::BITS {
        capping_u64_to_i64(val as u64)
    } else {
        i64::MAX
    }
}

/// Make sure the u64 fits into an i64.
///
/// Will return i64::MAX if the value would be bigger.
///
/// ```rust
/// assert_eq!(capping_u64_to_i64(u64::MAX), i64::MAX);
/// assert_eq!(capping_u64_to_i64(1), 1);
/// ```
fn capping_u64_to_i64(val: u64) -> i64 {
    0_i64.checked_add_unsigned(val).unwrap_or(i64::MAX)
}

/// If a positive value is passed it will be converted to it's negative value.
///
/// ```rust
/// assert_eq!(ensure_i32_is_negative(-1), -1);
/// assert_eq!(ensure_i32_is_negative(1), -1);
/// ```
fn ensure_i32_is_negative(val: i32) -> i32 {
    if val > 0 {
        return -val;
    } else {
        return val;
    }
}

impl LogFile {
    /// Create a new virtual log file.
    ///
    /// ```rust
    /// assert!(LogFile::new("test_log_file").is_ok());
    /// assert!(LogFile::new("a/path").is_err());
    /// ```
    pub fn new(name: &str) -> Result<Self> {
        const PATH_SEPARATORS: &[char; 2] = &['/', '\\'];
        if name.contains(PATH_SEPARATORS) {
            return Err(Error::new(
                ErrorKind::InvalidInput,
                "File name must not contain path separator",
            ));
        }

        log::trace!("Create new logfile from name '{name}'");
        let file_path = Self::file_path(name);

        let file = File::open(&file_path)?;
        Ok(LogFile { file })
    }

    /// Create a new logfile for the specified `path`.
    ///
    /// ```rust
    /// assert!(LogFile::new("test_log_file").is_ok());
    /// assert!(LogFile::new("./tests/test_log_file").is_ok());
    /// assert!(LogFile::new("/var/log/messages").is_ok());
    /// assert!(LogFile::new("a/path/that/doesnt/exist").is_err());
    /// ```
    pub fn from_path(path: &str) -> Result<Self> {
        log::trace!("Create new logfile from path '{path}'");
        let file = File::open(&path)?;
        Ok(LogFile { file })
    }

    fn file_path(name: &str) -> String {
        let path = format!("{}/{}", Self::log_path_prefix(), name);
        log::trace!("Generated filepath  for '{name}' is '{path}'");

        path
    }

    pub fn path_exists(path: &str) -> bool {
        Path::new(path).exists()
    }

    pub fn exists(name: &str) -> bool {
        Self::path_exists(Self::file_path(name).as_str())
    }

    fn log_path_prefix() -> &'static str {
        #[cfg(test)]
        return concat!(env!("CARGO_MANIFEST_DIR"), "/tests/");
        #[cfg(not(test))]
        "/var/log/"
    }

    /// Read data from the current position in the file.
    ///
    /// At most `min(i64::MAX, usize::MAX)` data is going to be valid in `buf`.
    pub fn read(&mut self, buf: &mut [u8]) -> std::io::Result<i64> {
        const MAX_READ: usize = i64::MAX as usize;

        let read = if buf.len() > MAX_READ {
            log::debug!(
                "Requested read of size {}, capping at {MAX_READ}",
                buf.len()
            );
            self.file.read(&mut buf[..MAX_READ])?
        } else {
            self.file.read(buf)?
        };

        Ok(capping_usize_to_i64(read))
    }

    /// Get the inode value of the current file.
    ///
    /// Returns `0` in case of a problem.
    pub fn get_inode(&self) -> u64 {
        match self.file.metadata() {
            Ok(metadata) => {
                return metadata.ino();
            }
            Err(err) => {
                log::error!("Couldn't get inode because: {err}");
                // Technically not an invalid value but unusual enough to indicate a
                // problem.
                return 0;
            }
        }
    }

    /// Get the length of the file in bytes.
    ///
    /// At most `i64::MAX` bytes will be returned even if the file is larger.
    /// In case of an error a negative value is returned.
    pub fn get_file_length(&self) -> i64 {
        match self.file.metadata() {
            Ok(metadata) => {
                let size = metadata.len();
                return capping_u64_to_i64(size);
            }
            Err(err) => {
                if let Some(os_err) = err.raw_os_error() {
                    return ensure_i32_is_negative(os_err) as i64;
                }
            }
        }
        // Generic negative value.
        return -1;
    }

    /// Set the current file position to `location`.
    pub fn seek(&mut self, location: SeekLocation) -> bool {
        let position = match location {
            SeekLocation::Begin => SeekFrom::Start(0),
            SeekLocation::BeforeEnd => SeekFrom::End(-1),
            SeekLocation::End => SeekFrom::End(0),
            SeekLocation {
                repr: 3_u8..=u8::MAX,
            } => {
                log::error!("Received invalid enum value '{location:?}' as seek location.");
                return false;
            }
        };

        match self.file.seek(position) {
            Ok(_) => true,
            Err(err) => {
                log::error!("Couldn't seek to {location:?} because: {err}");
                false
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    pub struct TestLogFile {
        log_file: NamedTempFile,
    }

    impl TestLogFile {
        pub fn new() -> Self {
            TestLogFile {
                log_file: NamedTempFile::new().expect("able to create temp file"),
            }
        }

        pub fn with_content(data: &str) -> Self {
            let mut log_file = NamedTempFile::new().expect("able to create temp file");

            write!(log_file, "{}", data).expect("can write to temp file");

            TestLogFile { log_file }
        }

        pub fn get_path(&self) -> &str {
            self.log_file.path().to_str().unwrap()
        }

        pub fn as_logfile(&self) -> LogFile {
            LogFile::from_path(self.get_path()).unwrap()
        }
    }

    fn buf_to_str(buf: &Vec<u8>, size: i64) -> &str {
        std::str::from_utf8(&buf[..size as usize]).unwrap()
    }

    // stat -c %s soul/gravedigger/tests/test_log_file
    const TEST_FILE_LENGTH: i64 = 56;

    #[test]
    fn no_path_allowed() {
        assert!(LogFile::new("/Some/Absolute/Path").is_err());
        assert!(LogFile::new("Some/Relative/Path").is_err());
        assert!(LogFile::new("C:\\\\WinPath").is_err());
        assert!(LogFile::new("test_log_file").is_ok());
    }

    #[test]
    fn file_length() {
        let lf = LogFile::new("test_log_file").unwrap();
        assert_eq!(lf.get_file_length(), TEST_FILE_LENGTH);

        let empty_file = TestLogFile::new();
        assert_eq!(empty_file.as_logfile().get_file_length(), 0);
    }

    #[test]
    fn read_test_file() {
        let data = "Hello World";
        let logfile = TestLogFile::with_content(data);
        let mut buf = vec![0; 1024];
        let read_bytes = logfile.as_logfile().read(buf.as_mut_slice()).unwrap();
        assert_eq!(read_bytes, data.len() as i64);
        assert_eq!(data, buf_to_str(&buf, read_bytes));
    }

    #[test]
    fn read_test_file_intervals() {
        let data = "Hello World";
        let test_file = TestLogFile::with_content(data);
        let mut logfile = test_file.as_logfile();
        let interval = data.len() / 3;
        let mut buf = vec![0; interval];
        let interval = interval as i64;
        for _ in 0..interval {
            let read_bytes = logfile.read(buf.as_mut_slice()).unwrap();
            assert_eq!(read_bytes, interval);
        }
        let read_bytes = logfile.read(buf.as_mut_slice()).unwrap();
        assert_eq!(read_bytes, (data.len() % 3) as i64);
    }

    #[test]
    fn read_test_file_large_buf() {
        let mut lf = LogFile::new("test_log_file").unwrap();
        let mut buf = vec![0; 1000];
        assert!((lf.get_file_length() as usize) < buf.len());
        let read_bytes = lf.read(buf.as_mut_slice()).unwrap();
        assert_eq!(read_bytes, TEST_FILE_LENGTH);
    }

    #[test]
    fn seek_begin() {
        let data = "Hello World";
        let test_file = TestLogFile::with_content(data);
        let mut logfile = test_file.as_logfile();
        let mut buf = vec![0; data.len()];

        assert_eq!(logfile.read(buf.as_mut_slice()).unwrap(), data.len() as i64);
        assert_eq!(logfile.read(buf.as_mut_slice()).unwrap(), 0);
        assert!(logfile.seek(SeekLocation::Begin));
        assert_eq!(logfile.read(buf.as_mut_slice()).unwrap(), data.len() as i64);
    }

    #[test]
    fn seek_before_end() {
        let data = "Hello World";
        let test_file = TestLogFile::with_content(data);
        let mut logfile = test_file.as_logfile();
        let mut buf = vec![0; data.len()];

        assert_eq!(logfile.get_file_length(), data.len() as i64);
        assert!(logfile.seek(SeekLocation::BeforeEnd));
        assert_eq!(logfile.read(buf.as_mut_slice()).unwrap(), 1);
    }

    #[test]
    fn seek_end() {
        let data = "Hello World";
        let test_file = TestLogFile::with_content(data);
        let mut logfile = test_file.as_logfile();
        let mut buf = vec![0; data.len()];

        assert_eq!(logfile.get_file_length(), data.len() as i64);
        assert!(logfile.seek(SeekLocation::End));
        assert_eq!(logfile.read(buf.as_mut_slice()).unwrap(), 0);
    }

    #[test]
    fn seek_invalid() {
        let mut logfile = LogFile::new("test_log_file").unwrap();

        assert!(!logfile.seek(SeekLocation { repr: 4_u8 }));
    }
}
