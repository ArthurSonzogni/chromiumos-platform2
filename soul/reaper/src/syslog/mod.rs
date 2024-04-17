// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Constants and utilities for interacting with syslog compatible programs.

mod facility;
mod message;
mod rfc3164_message;
mod severity;

pub use crate::syslog::facility::Facility;
pub use crate::syslog::message::SyslogMessage;
pub use crate::syslog::rfc3164_message::Rfc3164Message;
pub use crate::syslog::severity::Severity;
