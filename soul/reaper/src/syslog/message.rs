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
