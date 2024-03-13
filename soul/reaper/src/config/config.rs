// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde::Deserialize;

use crate::config::{log_file::FileName, program::RawProgram, Facility, Program};
use crate::syslog::Severity;

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub(super) struct RawConfig {
    pub(super) default_file_name: FileName,
    pub(super) default_file_severity: Severity,
    #[serde(default)]
    pub(super) programs: HashMap<Box<str>, RawProgram>,
    #[serde(default)]
    pub(super) facilities: HashMap<Box<str>, Facility>,
}

/// Main config structure containing the information from the main file and any
/// supplemental files.
///
/// This struct carries all data and should probably exist only once in your
/// program.
/// Most fields and fields of fields are eagerly initialized meaning they don't
/// contain `Optional<>` values but instead empty containers for faster read
/// access. Fields marked `Optional<>` have special meaning and can replace
/// other config options or alter the typical way of interacting with messages.
///
/// # Minimal config
/// ```toml
/// default_file_name = "messages"
/// default_severity = "info"
/// ```
#[derive(Debug, PartialEq)]
pub struct Config {
    /// The file name where all messages will be sent if there is no
    /// configuration for the program in `programs`.
    pub default_file_name: FileName,
    /// Only messages of this or higher severity will be logged in the
    /// `default_file_name`.
    pub default_file_severity: Severity,
    /// More detailed configurations for individual programs. The key is the
    /// name of the program unless `Program::match_name` is set.
    pub programs: HashMap<Box<str>, Program>,
    /// Syslog facilities with the facility name as key and the processing as
    /// value.
    pub facilities: HashMap<Box<str>, Facility>,
}

impl RawConfig {
    pub(super) fn set_program(&mut self, name: Box<str>, program: RawProgram) {
        if self.programs.contains_key(&name) {
            // We're keeping the latest value as it's likely that a system
            // default was overwritten with a specific supplemental config file
            // which is more relevant.
            log::warn!("Config already had configuration for {name}, using latest value");
        }
        self.programs.insert(name, program);
    }

    pub(super) fn eager(self) -> Config {
        Config {
            default_file_name: self.default_file_name,
            default_file_severity: self.default_file_severity,
            programs: self
                .programs
                .into_iter()
                .map(|(name, program)| (name, program.eager(self.default_file_severity)))
                .collect(),
            facilities: self.facilities,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::LogFile;

    #[test]
    fn eager_but_empty_containers() {
        let want = Config {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::new(),
            facilities: HashMap::new(),
        };

        let raw = RawConfig {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::new(),
            facilities: HashMap::new(),
        };

        assert_eq!(want, raw.eager());
    }

    #[test]
    fn set_program() {
        let want = Config {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::from([(
                Box::from("foo"),
                Program {
                    log_file: LogFile {
                        file_name: FileName::new("test"),
                        sync_file: false,
                        debug_critical: false,
                    },
                    severity: Severity::Critical,
                    match_name: None,
                    default_file_severity: None,
                },
            )]),
            facilities: HashMap::new(),
        };

        let mut raw = RawConfig {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::new(),
            facilities: HashMap::new(),
        };
        raw.set_program(
            Box::from("foo"),
            RawProgram {
                log_file: LogFile {
                    file_name: FileName::new("test"),
                    sync_file: false,
                    debug_critical: false,
                },
                severity: None,
                match_name: None,
                default_file_severity: None,
            },
        );

        assert_eq!(want, raw.eager());
    }

    #[test]
    fn only_keep_latest_program() {
        let want = Config {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::from([(
                Box::from("foo"),
                Program {
                    log_file: LogFile {
                        file_name: FileName::new("test"),
                        sync_file: false,
                        debug_critical: false,
                    },
                    severity: Severity::Critical,
                    match_name: Some(Box::from(".*")),
                    default_file_severity: None,
                },
            )]),
            facilities: HashMap::new(),
        };

        let mut raw = RawConfig {
            default_file_name: FileName::new("test"),
            default_file_severity: Severity::Critical,
            programs: HashMap::new(),
            facilities: HashMap::new(),
        };
        raw.set_program(
            Box::from("foo"),
            RawProgram {
                log_file: LogFile {
                    file_name: FileName::new("test"),
                    sync_file: false,
                    debug_critical: false,
                },
                severity: None,
                match_name: None,
                default_file_severity: None,
            },
        );
        raw.set_program(
            Box::from("foo"),
            RawProgram {
                log_file: LogFile {
                    file_name: FileName::new("test"),
                    sync_file: false,
                    debug_critical: false,
                },
                severity: None,
                match_name: Some(Box::from(".*")),
                default_file_severity: None,
            },
        );

        assert_eq!(want, raw.eager());
    }
}
