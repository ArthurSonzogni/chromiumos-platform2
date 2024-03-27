// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::fmt::{Error, Formatter};

use chrono::{DateTime, Utc};

use crate::syslog::Severity;

#[derive(PartialEq)]
pub struct Message {
    /// Also called `tag` in syslog lingo.
    pub application_name: Box<str>,
    pub message: Box<str>,
    pub severity: Severity,
    pub timestamp: DateTime<Utc>,
}

impl std::fmt::Debug for Message {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        f.debug_struct("Message")
            .field("application_name", &self.application_name)
            .field("message", &self.message())
            .field("severity", &self.severity)
            .field("timestamp", &self.timestamp)
            .finish()
    }
}

impl Message {
    #[cfg(not(test))]
    fn message(&self) -> &str {
        "(redacted)"
    }

    #[cfg(test)]
    fn message(&self) -> &str {
        &self.message
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    pub fn new_test_message() -> Message {
        Message {
            application_name: Box::from("unit test"),
            message: Box::from("running a unit test"),
            severity: Severity::Informational,
            timestamp: chrono::offset::Utc::now(),
        }
    }
}
