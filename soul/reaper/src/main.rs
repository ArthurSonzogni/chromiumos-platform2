// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the ChromeOS syslogd(8).
//!
//! `reaper` will read the mandatory config file from `/etc/soul.toml` and
//! any optional files matching `/etc/soul.d/*.toml` for additional config
//! values.

mod config;
mod intake_queue;
mod message;
mod syslog;

use anyhow::{Context, Result};

use crate::intake_queue::IntakeQueue;

const INTAKE_QUEUE_CAPACITY: usize = 3;

#[tokio::main]
async fn main() -> Result<()> {
    env_logger::init();
    let _config = config::read().context("Failed to read config from disk")?;

    let mut _intake_queue =
        IntakeQueue::new(INTAKE_QUEUE_CAPACITY).context("Couldn't create intake queue")?;

    Ok(())
}
