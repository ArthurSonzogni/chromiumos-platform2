// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use anyhow::Context;
use anyhow::Result;
use async_trait::async_trait;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus::nonblock::Proxy;
use dbus::nonblock::SyncConnection;
use log::error;
use tokio::sync::mpsc::unbounded_channel;
use tokio::sync::mpsc::UnboundedReceiver;

use crate::dbus::DEFAULT_DBUS_TIMEOUT;

#[async_trait]
pub trait DbusOwnershipChangeCallback: Send {
    async fn on_ownership_change(&self, old: String, new: String) -> Result<()>;
}

struct NameOwnerChangeInfo {
    old: String,
    new: String,
    // Indicates whether the information is from a dbus signal or from a
    // GetNameOwner query during initialization.
    from_signal: bool,
}

async fn handle_name_owner_changes<T: DbusOwnershipChangeCallback>(
    mut receiver: UnboundedReceiver<NameOwnerChangeInfo>,
    cb: T,
) {
    let mut has_received_message = false;
    while let Some(msg) = receiver.recv().await {
        // We need to poll dbus to detect if a service is owned when we start
        // monitoring. If we receive a signal before we get the chance to process
        // that polled information, then the polled information is either
        // redundant or stale.
        if has_received_message && !msg.from_signal {
            continue;
        }

        has_received_message = true;
        let res = cb.on_ownership_change(msg.old, msg.new);
        if let Err(e) = res.await {
            error!("Error handling name owner change: {:?}", e);
        }
    }
}

/// Invoke the given callback on any name ownership changes. If the name
/// is already owned when this function is called, the callback will be
/// invoked with the current owner.
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

    tokio::spawn(handle_name_owner_changes(receiver, cb));

    let sender = Arc::new(sender);
    let cb_sender = sender.clone();
    conn.start_receive(
        name_owner_change_signal,
        Box::new(move |msg, _| {
            let (name, old, new): (String, String, String) = match msg.read3() {
                Ok(res) => res,
                Err(e) => {
                    error!("Malformed signal: {:?}", e);
                    return true;
                }
            };

            if name == service_name {
                let msg = NameOwnerChangeInfo {
                    old,
                    new,
                    from_signal: true,
                };
                if let Err(e) = cb_sender.send(msg) {
                    error!("error dispatching name owner change: {:?}", e)
                }
            }
            true
        }),
    );

    let proxy = Proxy::new(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        DEFAULT_DBUS_TIMEOUT,
        conn.clone(),
    );

    // This method fails if the name isn't owned, so we can't differentiate a failure
    // from an unowned name. Trying to check NameHasOwner before calling GetNameOwner
    // would help a little, but it would still be racy. However, if there is a
    // failure, then something major is probably wrong with dbus, so assuming the
    // name isn't owned is okay.
    let (name,): (String,) = proxy
        .method_call("org.freedesktop.DBus", "GetNameOwner", (service_name,))
        .await
        .unwrap_or((String::new(),));
    if !name.is_empty() {
        let msg = NameOwnerChangeInfo {
            old: String::new(),
            new: name,
            from_signal: false,
        };
        if let Err(e) = sender.send(msg) {
            error!("error dispatching name owner change: {:?}", e)
        }
    }

    Ok(())
}
