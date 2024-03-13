// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde::Deserialize;

use crate::config::LogFile;
use crate::syslog::Severity;

/// Allows filtering and redirecting based on syslog facilities.
///
/// # Example config
/// ```toml
/// default_file_name = "messages"
/// default_severity = "info"
///
/// [facilities]
/// kern = { devices = [
///   { "info"= "/dev/console" }
/// ] }
/// authpriv = { files = [
///   { "*"={ file_name="secure", sync_file=true } }
/// ], skip_default_file=true }
/// ```
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct Facility {
    /// Write messages from this facility of this severity and higher to the
    /// device specified in the value.
    #[serde(default)]
    pub devices: Vec<HashMap<Severity, Box<str>>>,
    /// Write messages from this facility of this severity and higher to the
    /// log file.
    #[serde(default)]
    pub files: Vec<HashMap<Severity, LogFile>>,
    /// Skip any messages from this facility, that would be sent to a device or
    /// a file, in the `Config::default_file_name`.
    /// Defaults to `false`.
    #[serde(default)]
    pub skip_default_file: bool,
}
