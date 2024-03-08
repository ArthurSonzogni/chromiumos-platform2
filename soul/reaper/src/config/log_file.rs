// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{ensure, Result};
use serde::Deserialize;

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(try_from = "Box<Path>")]
pub struct FileName(Box<str>);

impl TryFrom<Box<Path>> for FileName {
    type Error = anyhow::Error;

    fn try_from(value: Box<Path>) -> Result<Self> {
        Self::is_valid_name(&value)?;
        Ok(Self(value.to_string_lossy().into()))
    }
}

impl FileName {
    #[cfg(test)]
    pub(super) fn new(file_name: &str) -> Self {
        // Only do a plain unwrap() here because it's only used in tests.
        Self::is_valid_name(&Path::new(file_name)).unwrap();

        FileName(Box::from(file_name))
    }

    fn is_valid_name(file_name: &Path) -> Result<()> {
        let file_str = file_name.as_os_str().to_string_lossy();
        ensure!(!file_str.is_empty(), "Logfile name must not be empty");
        ensure!(
            !file_str.contains(std::path::MAIN_SEPARATOR),
            "Logfile name '{file_str}' must not be a path"
        );
        ensure!(
            !file_name.starts_with("."),
            "Logfile name '{file_str}' must not start with a period"
        );

        Ok(())
    }
}

/// The file to which a program should write its messages.
///
/// This structure should never appear alone in the config file but only as
/// value for other keys in higher level structures.
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct LogFile {
    /// File name where logs from this program will be stored.
    pub file_name: FileName,
    /// Sync the file to disk after every message from the program. Defaults to
    /// `false`.
    #[serde(default)]
    pub sync_file: bool,
    /// Only use this for logs that are critical to debug basic functionality,
    /// e.g. when the system can't start otherwise.
    /// Before setting this please open a ticket in the component
    /// listed in DIR_METADATA.
    /// Defaults to `false`.
    #[serde(default)]
    pub debug_critical: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid_file_name() {
        assert!(FileName::is_valid_name(Path::new("foo")).is_ok());
        assert!(FileName::is_valid_name(Path::new("net.log")).is_ok());
    }

    #[test]
    fn invalid_file_name() {
        assert_eq!(
            format!("{}", FileName::is_valid_name(Path::new("")).unwrap_err()),
            "Logfile name must not be empty"
        );
        assert_eq!(
            format!(
                "{}",
                FileName::is_valid_name(Path::new("relative/path")).unwrap_err()
            ),
            "Logfile name 'relative/path' must not be a path"
        );
        assert_eq!(
            format!(
                "{}",
                FileName::is_valid_name(Path::new("/absolute/path")).unwrap_err()
            ),
            "Logfile name '/absolute/path' must not be a path"
        );
        assert_eq!(
            format!("{}", FileName::is_valid_name(Path::new("/")).unwrap_err()),
            "Logfile name '/' must not be a path"
        );
        assert_eq!(
            format!("{}", FileName::is_valid_name(Path::new(".")).unwrap_err()),
            "Logfile name '.' must not start with a period"
        );
        assert_eq!(
            format!("{}", FileName::is_valid_name(Path::new("../")).unwrap_err()),
            "Logfile name '../' must not be a path"
        );
    }
}
