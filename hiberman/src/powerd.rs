// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements a basic power_manager client.

use std::time::Duration;

use anyhow::{Context as AnyhowContext, Result};
use dbus::blocking::Connection;
use log::{debug, error, info};
use system_api::client::OrgChromiumPowerManager;

/// Values for the flavor parameter of powerd's RequestSuspend method.
#[repr(u32)]
enum PowerdSuspendFlavor {
    FromDiskPrepare = 3,
    FromDiskAbort = 4,
}

/// Implements a pending resume ticket. When created, it tells powerd to prepare
/// for imminent resume. When dropped, notifies powerd that the resume has been
/// aborted.
pub struct PowerdPendingResume {}

impl PowerdPendingResume {
    pub fn new() -> Result<Self> {
        powerd_request_suspend(PowerdSuspendFlavor::FromDiskPrepare)?;
        Ok(PowerdPendingResume {})
    }
}

impl Drop for PowerdPendingResume {
    fn drop(&mut self) {
        if let Err(e) = powerd_request_suspend(PowerdSuspendFlavor::FromDiskAbort) {
            error!("Failed to notify powerd of aborted resume: {}", e);
        }
    }
}

/// Helper function to make a simple RequestSuspend d-bus call to powerd.
fn powerd_request_suspend(flavor: PowerdSuspendFlavor) -> Result<()> {
    // First open up a connection to the session bus.
    let conn = Connection::new_system().context("Failed to start system dbus connection")?;

    // Second, create a wrapper struct around the connection that makes it easy
    // to send method calls to a specific destination and path.
    let proxy = conn.with_proxy(
        "org.chromium.PowerManager",
        "/org/chromium/PowerManager",
        Duration::from_millis(60000),
    );

    let external_wakeup_count: u64 = u64::MAX;
    let wakeup_timeout: i32 = 0;
    let flavor = flavor as u32;
    info!("Calling powerd RequestSuspend, flavor {}", flavor);
    proxy.request_suspend(external_wakeup_count, wakeup_timeout, flavor)?;
    debug!("RequestSuspend returned");
    Ok(())
}
