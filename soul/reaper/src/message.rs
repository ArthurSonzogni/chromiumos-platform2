// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::fmt::{Error, Formatter};

use chrono::{DateTime, Utc};

use crate::syslog::{Facility, Severity, SyslogMessage};

#[derive(PartialEq)]
pub struct Message {
    /// Also called `tag` in syslog lingo.
    pub application_name: Box<str>,
    pub facility: Facility,
    pub message: Box<str>,
    pub severity: Severity,
    pub timestamp: DateTime<Utc>,
}

impl std::fmt::Debug for Message {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        f.debug_struct("Message")
            .field("application_name", &self.application_name)
            .field("facility", &self.facility)
            .field("message", &redact(&self.message))
            .field("severity", &self.severity)
            .field("timestamp", &self.timestamp)
            .finish()
    }
}

impl From<Box<dyn SyslogMessage>> for Message {
    fn from(message: Box<dyn SyslogMessage>) -> Self {
        Message {
            application_name: Box::from(message.application_name()),
            facility: message.facility(),
            message: Box::from(message.message()),
            severity: message.severity(),
            timestamp: message.timestamp(),
        }
    }
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

#[cfg(test)]
pub mod tests {
    use super::*;

    pub fn new_test_message() -> Message {
        Message {
            application_name: Box::from("unit test"),
            facility: Facility::User,
            message: Box::from("running a unit test"),
            severity: Severity::Informational,
            timestamp: chrono::offset::Utc::now(),
        }
    }
}
