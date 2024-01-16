// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides a thin wrapper for interacting with the SwapManagement service.

use std::time::Duration;

use anyhow::Context;
use anyhow::Result;
use dbus::blocking::Connection;
use dbus::blocking::Proxy;

use system_api::client::OrgChromiumSwapManagement;

use crate::hiberutil::get_page_size;

// Define the timeout to connect to the dbus system.
const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

const SWAP_MANAGEMENT_DBUS_NAME: &str = "org.chromium.SwapManagement";
const SWAP_MANAGEMENT_DBUS_PATH: &str = "/org/chromium/SwapManagement";

// Types of reclaimable memory.
const RECLAIM_ANON: u8 = 0x01;
const RECLAIM_SHMEM: u8 = 0x02;
#[allow(dead_code)]
const RECLAIM_FILE: u8 = 0x04;

// The allowed modes of operation for zram writeback. The definition is in:
// src/platform2/swap_management/dbus_bindings/org.chromium.SwapManagement.xml
#[allow(dead_code)]
pub enum WritebackMode {
    Idle = 1,
    Huge = 2,
    IdleAndHuge = 4,
}

pub fn reclaim_all_processes() -> Result<()> {
    let conn = get_dbus_connection()?;
    let proxy = get_sm_proxy(&conn);

    Ok(proxy.reclaim_all_processes(RECLAIM_ANON | RECLAIM_SHMEM)?)
}

pub fn swap_zram_set_writeback_limit(size_mb: u32) -> Result<()> {
    let conn = get_dbus_connection()?;
    let proxy = get_sm_proxy(&conn);

    let page_size = get_page_size() as u64;
    let num_pages = ((size_mb as u64 * 1024 * 1024) / page_size) as u32;

    Ok(proxy.swap_zram_set_writeback_limit(num_pages)?)
}

pub fn swap_zram_mark_idle(age: u32) -> Result<()> {
    let conn = get_dbus_connection()?;
    let proxy = get_sm_proxy(&conn);

    Ok(proxy.swap_zram_mark_idle(age)?)
}

pub fn initiate_swap_zram_writeback(mode: WritebackMode) -> Result<()> {
    let conn = get_dbus_connection()?;
    // The default timeout doesn't work in this case, since the callee
    // only returns after having written eligible data to disk, which
    // can take a longer time.
    let proxy = conn.with_proxy(
        SWAP_MANAGEMENT_DBUS_NAME,
        SWAP_MANAGEMENT_DBUS_PATH,
        Duration::from_secs(60),
    );

    Ok(proxy.initiate_swap_zram_writeback(mode as u32)?)
}

// Helper function for getting a D-Bus connection.
fn get_dbus_connection() -> Result<Connection> {
    Connection::new_system().context("Failed to connect to dbus for swap management")
}

// Helper for getting a SwapManagement proxy from a D-Bus connection.
fn get_sm_proxy(connection: &Connection) -> Proxy<'static, &Connection> {
    connection.with_proxy(
        SWAP_MANAGEMENT_DBUS_NAME,
        SWAP_MANAGEMENT_DBUS_PATH,
        DEFAULT_DBUS_TIMEOUT,
    )
}
