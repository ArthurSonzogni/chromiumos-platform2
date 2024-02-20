// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements a client interface to the update_engine.

use std::time::Duration;

use anyhow::anyhow;
use anyhow::Context as AnyhowContext;
use anyhow::Result;
use dbus::blocking::Connection;
use log::info;
use protobuf::Message;
use system_api::update_engine::Operation;
use system_api::update_engine::StatusResult;
use update_engine_dbus::client::OrgChromiumUpdateEngineInterface;

/// Define the default maximum duration the update_engine proxy will wait for method
/// call responses.
const UPDATE_ENGINE_DBUS_PROXY_TIMEOUT: Duration = Duration::from_secs(30);

pub fn is_update_in_progress() -> Result<bool> {
    let not_in_progress_ops = [Operation::IDLE, Operation::CHECKING_FOR_UPDATE,
                               Operation::UPDATE_AVAILABLE, Operation::DISABLED,
                               Operation::NEED_PERMISSION_TO_UPDATE];

    let status = get_status().context("Failed to get update engine status")?;
    let current_operation = match status.current_operation.enum_value() {
        Ok(op) => op,
        Err(e) => return Err(anyhow!("Failed to extract update engine enum value: {e}")),
    };

    if not_in_progress_ops.contains(&current_operation) {
        Ok(false)
    } else {
        info!("Update engine status is {:?}", current_operation);
        Ok(true)
    }
}

fn get_status() -> Result<StatusResult> {
    // First open up a connection to the system bus.
    let conn = Connection::new_system().context("Failed to start system dbus connection")?;

    // Second, create a wrapper struct around the connection that makes it easy
    // to send method calls to a specific destination and path.
    let proxy = conn.with_proxy(
        "org.chromium.UpdateEngine",
        "/org/chromium/UpdateEngine",
        UPDATE_ENGINE_DBUS_PROXY_TIMEOUT,
    );

    // Fire off the method call to update_engine.
    let result = proxy
        .get_status_advanced()
        .context("Failed to call UpdateEngine.GetStatusAdvanced")?;
    // Parse the resulting protobuf back into a structure.
    StatusResult::parse_from_bytes(&result).context("Failed to parse StatusResult protobuf")
}
