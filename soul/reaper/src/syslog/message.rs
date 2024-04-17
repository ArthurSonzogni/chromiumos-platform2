// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use chrono::{DateTime, Utc};

use crate::syslog::{Facility, Severity};

pub trait SyslogMessage: Send {
    fn application_name(&self) -> &str;
    fn facility(&self) -> Facility;
    fn message(&self) -> &str;
    fn severity(&self) -> Severity;
    fn timestamp(&self) -> DateTime<Utc>;
}

/// For non-test configs the returned string will always be `(redacted)`.
///
/// This is to prevent accidental leakage of message contents while processing.
/// The test config version of this method returns the full `message` input
/// to aid with debugging.
#[cfg(not(test))]
pub fn redact(_message: &str) -> &str {
    "(redacted)"
}

#[cfg(test)]
pub fn redact(message: &str) -> &str {
    message
}
