// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use chrono::{DateTime, Utc};

use crate::syslog::{Facility, Severity, SyslogMessage};

pub struct Rfc5424Message {}

impl SyslogMessage for Rfc5424Message {
    fn application_name(&self) -> &str {
        todo!()
    }

    fn facility(&self) -> Facility {
        todo!()
    }

    fn message(&self) -> &str {
        todo!()
    }

    fn severity(&self) -> Severity {
        todo!()
    }
    fn timestamp(&self) -> DateTime<Utc> {
        todo!()
    }
}

impl TryFrom<&str> for Rfc5424Message {
    type Error = anyhow::Error;

    fn try_from(_data: &str) -> Result<Self> {
        bail!("todo")
    }
}
