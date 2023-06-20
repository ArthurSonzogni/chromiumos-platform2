// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Collections of methods to send D-BUS signals

use crate::{dbus_constants, dbus_wrapper::DbusConnectionTrait};
use anyhow::{anyhow, Result};
use libchromeos::sys::debug;
use std::sync::Arc;
use system_api::shadercached::ShaderCacheMountStatus;

pub fn signal_mount_status<D: DbusConnectionTrait>(
    mount_status_many: Vec<ShaderCacheMountStatus>,
    dbus_conn: Arc<D>,
) -> Result<()> {
    let mut errors: Vec<String> = vec![];
    for status in mount_status_many {
        if let Err(e) = emit_signal(
            &status,
            dbus_constants::MOUNT_STATUS_CHANGED_SIGNAL,
            dbus_conn.clone(),
        ) {
            errors.push(e.to_string());
        }
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(anyhow!("Failed to signal some status: {:?}", errors))
    }
}

fn emit_signal<D: DbusConnectionTrait>(
    to_emit: &(impl protobuf::Message + std::fmt::Debug),
    signal_name: &str,
    dbus_conn: Arc<D>,
) -> Result<()> {
    // Tell Cicerone shader cache has been (un)mounted, so that it can continue
    // process calls.
    let mounted_signal = dbus::Message::new_signal(
        dbus_constants::PATH_NAME,
        dbus_constants::INTERFACE_NAME,
        signal_name,
    )
    .map_err(|e| anyhow!("Failed to create signal: {}", e))?
    .append1(
        to_emit
            .write_to_bytes()
            .map_err(|e| anyhow!("Failed to parse protobuf: {}", e))?,
    );
    debug!("Sending {} signal.. {:?}", signal_name, to_emit);
    dbus_conn
        .send(mounted_signal)
        .map_err(|_| anyhow!("Failed to send signal"))?;
    Ok(())
}
