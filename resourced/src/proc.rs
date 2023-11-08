// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;
use std::fs::File;
use std::io;
use std::io::BufRead;
use std::io::BufReader;

/// Error of parsing /proc/pid/status
#[derive(Debug)]
pub enum Error {
    NotFound(u32),
    FileCorrupt,
    Io(io::Error),
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::NotFound(_) => None,
            Self::FileCorrupt => None,
            Self::Io(e) => Some(e),
        }
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NotFound(pid) => f.write_fmt(format_args!("/proc/{pid}/status is not found")),
            Self::FileCorrupt => f.write_str("/proc/pid/status invalid format"),
            Self::Io(e) => f.write_fmt(format_args!("procfs: {e}")),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

enum UidIndex {
    Real = 0,
    Effective = 1,
}

/// Load real user id of the process from /proc/pid/status
pub fn load_ruid(pid: u32) -> Result<u32> {
    let mut file = open_status_file(pid)?;
    load_uid(&mut file, UidIndex::Real)
}

/// Load effective user id of the process from /proc/pid/status
pub fn load_euid(pid: u32) -> Result<u32> {
    let mut file = open_status_file(pid)?;
    load_uid(&mut file, UidIndex::Effective)
}

fn open_status_file(pid: u32) -> Result<File> {
    File::open(format!("/proc/{}/status", pid)).map_err(|e| {
        if e.kind() == io::ErrorKind::NotFound {
            Error::NotFound(pid)
        } else {
            Error::Io(e)
        }
    })
}

fn load_uid(file: &mut File, i_uid: UidIndex) -> Result<u32> {
    let r = BufReader::with_capacity(1024, file);
    for line in r.lines() {
        let line = line.map_err(Error::Io)?;
        const UID_TAG: &str = "Uid:";
        if let Some(uids) = line.strip_prefix(UID_TAG) {
            return uids
                .split_whitespace()
                .nth(i_uid as usize)
                .and_then(|uid| uid.parse().ok())
                .ok_or(Error::FileCorrupt);
        }
    }
    Err(Error::FileCorrupt)
}

#[cfg(test)]
mod tests {
    use std::io::Seek;
    use std::io::SeekFrom;
    use std::io::Write;

    use super::*;
    use crate::proc::load_ruid;

    #[test]
    fn test_load_ruid() {
        assert!(load_ruid(std::process::id()).is_ok());
        assert!(matches!(
            load_ruid(u32::MAX),
            Err(Error::NotFound(u32::MAX))
        ));
    }

    #[test]
    fn test_load_euid() {
        assert!(load_euid(std::process::id()).is_ok());
        assert!(matches!(
            load_euid(u32::MAX),
            Err(Error::NotFound(u32::MAX))
        ));
    }

    #[test]
    fn test_load_uid() {
        const STATUS_FILE: &str = "Name:   resourced
Umask:  0022
State:  S (sleeping)
Tgid:   12153
Ngid:   0
Pid:    12153
PPid:   1
TracerPid:      0
Uid:    20170   20171   20172   20173
Gid:    20174   20175   20176   20177";
        let mut file = tempfile::tempfile().unwrap();
        file.write_all(STATUS_FILE.as_bytes()).unwrap();
        file.seek(SeekFrom::Start(0)).unwrap();

        assert_eq!(load_uid(&mut file, UidIndex::Real).unwrap(), 20170);
        file.seek(SeekFrom::Start(0)).unwrap();
        assert_eq!(load_uid(&mut file, UidIndex::Effective).unwrap(), 20171);
    }
}
