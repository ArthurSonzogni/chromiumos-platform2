// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

/// Enum representing syslog severities.
///
/// Allows full severity name, keyword and numeric value.
#[derive(Copy, Clone, Debug, Deserialize, PartialEq, PartialOrd)]
pub enum Severity {
    #[serde(alias = "emerg", alias = "0")]
    Emergency = 0,
    #[serde(alias = "alert", alias = "1")]
    Alert = 1,
    #[serde(alias = "crit", alias = "2")]
    Critical = 2,
    #[serde(alias = "err", alias = "3")]
    Error = 3,
    #[serde(alias = "warning", alias = "4")]
    Warning = 4,
    #[serde(alias = "notice", alias = "5")]
    Notice = 5,
    #[serde(alias = "info", alias = "6")]
    Informational = 6,
    #[serde(alias = "debug", alias = "7")]
    Debug = 7,
}
