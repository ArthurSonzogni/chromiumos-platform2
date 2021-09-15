// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod dbus;
mod gpu_freq_scaling;
mod memory;

#[cfg(test)]
mod test;

use anyhow::{bail, Result};
use sys_util::syslog;

fn main() -> Result<()> {
    if let Err(e) = syslog::init() {
        bail!("Failed to initiailize syslog: {}", e);
    }

    dbus::service_main()
}
