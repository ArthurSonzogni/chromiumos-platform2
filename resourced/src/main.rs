// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod dbus;
mod memory;

#[cfg(test)]
mod test;

use std::thread;

use anyhow::{bail, Result};
use sys_util::syslog;

fn main() -> Result<()> {
    if let Err(e) = syslog::init() {
        bail!("Failed to initiailize syslog: {}", e);
    }

    // The D-Bus service should be initialized before starting the memory
    // checking thread.
    let context = dbus::service_init()?;

    thread::spawn(dbus::check_memory_main);

    dbus::service_main_loop(context)
}
