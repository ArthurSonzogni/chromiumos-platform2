// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the ChromeOS syslogd(8).
//!
//! `reaper` will read the mandatory config file from `/etc/soul.toml` and
//! any optional files matching `/etc/soul.d/*.toml` for additional config
//! values.

mod config;
mod syslog;

use anyhow::Result;

fn main() -> Result<()> {
    env_logger::init();
    let config = config::read()?;

    println!("{config:#?}");

    Ok(())
}
