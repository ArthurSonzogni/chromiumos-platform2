// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides a thin wrapper for interacting with the SwapManagement service.

use std::time::Duration;

use anyhow::Context;
use anyhow::Result;
use dbus::blocking::Connection;

use system_api::client::OrgChromiumSwapManagement;

// Define the timeout to connect to the dbus system.
const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

const SWAP_MANAGEMENT_DBUS_NAME: &str = "org.chromium.SwapManagement";
const SWAP_MANAGEMENT_DBUS_PATH: &str = "/org/chromium/SwapManagement";

// Types of reclaimable memory.
const RECLAIM_ANON: u8 = 0x01;
const RECLAIM_SHMEM: u8 = 0x02;
#[allow(dead_code)]
const RECLAIM_FILE: u8 = 0x04;

pub fn reclaim_all_processes() -> Result<()> {
    let conn =
        Connection::new_system().context("Failed to connect to dbus for swap management")?;
    let proxy = conn.with_proxy(
        SWAP_MANAGEMENT_DBUS_NAME,
        SWAP_MANAGEMENT_DBUS_PATH,
        DEFAULT_DBUS_TIMEOUT,
    );

    Ok(proxy.reclaim_all_processes(RECLAIM_ANON | RECLAIM_SHMEM)?)
}
