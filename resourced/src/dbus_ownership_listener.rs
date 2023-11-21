// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use anyhow::Context;
use anyhow::Result;
use async_trait::async_trait;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus::nonblock::SyncConnection;
use dbus::Message;
use log::error;
use tokio::sync::mpsc::unbounded_channel;
use tokio::sync::mpsc::UnboundedReceiver;

#[async_trait]
pub trait DbusOwnershipChangeCallback: Send {
    async fn on_ownership_change(&self, old: String, new: String) -> Result<()>;
}

async fn handle_name_owner_changes<T: DbusOwnershipChangeCallback>(
    mut receiver: UnboundedReceiver<Message>,
    expected_service_name: &'static str,
    cb: T,
) {
    while let Some(msg) = receiver.recv().await {
        let (service_name, old, new): (String, String, String) = match msg.read3() {
            Ok(res) => res,
            Err(e) => {
                error!("Malformed signal: {:?}", e);
                continue;
            }
        };

        if service_name == expected_service_name {
            let res = cb.on_ownership_change(old, new);
            if let Err(e) = res.await {
                error!("Error handling name owner change: {:?}", e);
            }
        }
    }
}

pub async fn monitor_dbus_service<T: DbusOwnershipChangeCallback + 'static>(
    conn: &Arc<SyncConnection>,
    service_name: &'static str,
    cb: T,
) -> Result<()> {
    // MatchRule doesn't support matching by arguments, so manually construct a
    // match rule string that only listens for the target service related changes to
    // avoid unnecessary IPC. Since the MatchRule is less specific, we filter by
    // service name in the callback.
    let name_owner_match_string = [
        "interface=org.freedesktop.DBus".to_string(),
        "member=NameOwnerChanged".to_string(),
        format!("arg0={service_name}"),
    ]
    .join(",");

    let name_owner_change_signal =
        MatchRule::new_signal("org.freedesktop.DBus", "NameOwnerChanged");

    conn.add_match_no_cb(&name_owner_match_string)
        .await
        .context("failed to add match")?;

    let (sender, receiver) = unbounded_channel();

    tokio::spawn(handle_name_owner_changes(receiver, service_name, cb));

    conn.start_receive(
        name_owner_change_signal,
        Box::new(move |msg, _| {
            if let Err(e) = sender.send(msg) {
                error!("error dispatching name owner change: {:?}", e)
            }
            true
        }),
    );

    Ok(())
}
