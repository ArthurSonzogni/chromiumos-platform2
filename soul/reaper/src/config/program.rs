// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

use crate::config::LogFile;
use crate::syslog::Severity;

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub(super) struct RawProgram {
    #[serde(flatten)]
    pub(super) log_file: LogFile,
    pub(super) severity: Option<Severity>,
    pub(super) match_name: Option<Box<str>>,
    pub(super) default_file_severity: Option<Severity>,
}

/// Individual configuration for a program.
///
/// # Example config
/// ```toml
/// default_file_name = "messages"
/// default_severity = "info"
///
/// [programs]
/// shill = { file_name="net.log", severity="warning", default_file_severity="4" }
/// reaper = { file_name="reaper.log" }
/// ```
#[derive(Debug, PartialEq)]
pub struct Program {
    /// The log file of this program. The on-disk config has this flattened into
    /// this struct.
    pub log_file: LogFile,
    /// Messages of this severity and higher will be logged to `log_file`.
    pub severity: Severity,
    /// Instead of relying on the name(tag) of the program in the message use
    /// this expression to match names in the syslog tag.
    pub match_name: Option<Box<str>>,
    /// If set messages of this and higher severity will also be logged into
    /// `Config::default_file_name`.
    pub default_file_severity: Option<Severity>,
}

impl RawProgram {
    pub fn eager(self, default_severity: Severity) -> Program {
        Program {
            log_file: self.log_file,
            severity: self.severity.unwrap_or(default_severity),
            match_name: self.match_name,
            default_file_severity: self.default_file_severity,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::log_file::FileName;

    #[test]
    fn eager_all_none() {
        let want = Program {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: Severity::Error,
            match_name: None,
            default_file_severity: None,
        };
        let raw = RawProgram {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: None,
            match_name: None,
            default_file_severity: None,
        };

        assert_eq!(want, raw.eager(Severity::Error));
    }

    #[test]
    fn eager_with_severity() {
        let want = Program {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: Severity::Debug,
            match_name: None,
            default_file_severity: None,
        };
        let raw = RawProgram {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: Some(Severity::Debug),
            match_name: None,
            default_file_severity: None,
        };

        assert_eq!(want, raw.eager(Severity::Error));
    }

    #[test]
    fn eager_with_matcher() {
        let want = Program {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: Severity::Error,
            match_name: Some(Box::from(".*")),
            default_file_severity: None,
        };
        let raw = RawProgram {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: None,
            match_name: Some(Box::from(".*")),
            default_file_severity: None,
        };

        assert_eq!(want, raw.eager(Severity::Error));
    }

    #[test]
    fn eager_with_default_file() {
        let want = Program {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: Severity::Error,
            match_name: None,
            default_file_severity: Some(Severity::Emergency),
        };
        let raw = RawProgram {
            log_file: LogFile {
                file_name: FileName::new("test"),
                sync_file: false,
                debug_critical: false,
            },
            severity: None,
            match_name: None,
            default_file_severity: Some(Severity::Emergency),
        };

        assert_eq!(want, raw.eager(Severity::Error));
    }
}
