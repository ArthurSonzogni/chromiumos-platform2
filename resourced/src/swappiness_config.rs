// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;
use std::sync::Mutex;

use anyhow::Context;
use anyhow::Result;
use async_trait::async_trait;
use dbus::nonblock::Proxy;
use dbus::nonblock::SyncConnection;
use log::error;
use tokio::sync::mpsc::unbounded_channel;
use tokio::sync::mpsc::UnboundedReceiver;
use tokio::sync::mpsc::UnboundedSender;

use crate::common::read_from_file;
use crate::common::TuneSwappiness;
use crate::dbus::DEFAULT_DBUS_TIMEOUT;
use crate::dbus_ownership_listener::monitor_dbus_service;
use crate::dbus_ownership_listener::DbusOwnershipChangeCallback;
use crate::sync::NoPoison;

const DEFAULT_SWAPPINESS_VALUE: u32 = 60;

enum Message {
    UpdateDefaultSwappiness(u32),
    SetSwappinessState(Option<TuneSwappiness>),
    Connected,
}

#[derive(Clone)]
pub struct SwappinessConfig {
    sender: Arc<UnboundedSender<Message>>,
}

pub struct SwappinessConfigProxy {
    receiver: UnboundedReceiver<Message>,
    sender: Arc<UnboundedSender<Message>>,
}

pub fn new_swappiness_config() -> (SwappinessConfig, SwappinessConfigProxy) {
    let (sender, receiver) = unbounded_channel();
    let sender = Arc::new(sender);
    (
        SwappinessConfig {
            sender: sender.clone(),
        },
        SwappinessConfigProxy { receiver, sender },
    )
}

impl SwappinessConfig {
    pub fn update_tuning(&self, tuning: Option<TuneSwappiness>) {
        self.do_send(Message::SetSwappinessState(tuning))
    }

    pub fn update_default_swappiness(&self, swappiness: Option<u32>) {
        let swappiness = swappiness.unwrap_or(DEFAULT_SWAPPINESS_VALUE);
        self.do_send(Message::UpdateDefaultSwappiness(swappiness))
    }

    fn do_send(&self, msg: Message) {
        if let Err(err) = self.sender.send(msg) {
            error!("Error adjusting swappiness {:?}", err);
        }
    }
}

struct SwapManagementMonitor {
    connected: Arc<Mutex<bool>>,
    sender: Arc<UnboundedSender<Message>>,
}

#[async_trait]
impl DbusOwnershipChangeCallback for SwapManagementMonitor {
    async fn on_ownership_change(&self, _old: String, new: String) -> Result<()> {
        let mut connected = self.connected.do_lock();
        *connected = !new.is_empty();
        if *connected {
            if let Err(err) = self.sender.send(Message::Connected) {
                error!("Failed to handle connection {:?}", err);
            }
        }
        Ok(())
    }
}

impl SwappinessConfigProxy {
    pub async fn run_proxy(&mut self, conn: Arc<SyncConnection>) -> Result<()> {
        let mut default_swappiness = DEFAULT_SWAPPINESS_VALUE;
        let mut tuning: Option<TuneSwappiness> = None;
        let connected = Arc::new(Mutex::new(false));
        let swap_management_monitor = SwapManagementMonitor {
            connected: connected.clone(),
            sender: self.sender.clone(),
        };

        monitor_dbus_service(
            &conn,
            "org.chromium.SwapManagement",
            swap_management_monitor,
        )
        .await
        .context("failed to monitor swap management")?;

        let proxy = Proxy::new(
            "org.chromium.SwapManagement",
            "/org/chromium/SwapManagement",
            DEFAULT_DBUS_TIMEOUT,
            conn,
        );

        while let Some(msg) = self.receiver.recv().await {
            match msg {
                Message::UpdateDefaultSwappiness(val) => default_swappiness = val,
                Message::SetSwappinessState(new_tuning) => tuning = new_tuning,
                Message::Connected => {}
            }

            let new_swappiness = tuning.map_or(default_swappiness, |t| t.swappiness);

            const SWAPPINESS_PATH: &str = "/proc/sys/vm/swappiness";
            let system_swappiness = match read_from_file(&SWAPPINESS_PATH) {
                Ok(val) => val,
                Err(e) => {
                    error!("Failed to read swappiness {:?}", e);
                    continue;
                }
            };

            if !*connected.do_lock() {
                continue;
            }

            if new_swappiness != system_swappiness {
                let res: Result<(), dbus::Error> = proxy
                    .method_call(
                        "org.chromium.SwapManagement",
                        "SwapSetSwappiness",
                        (new_swappiness,),
                    )
                    .await;
                if let Err(e) = res {
                    error!("Calling SwapSetSwappiness failed: {:#}", e);
                }
            }
        }

        Ok(())
    }
}
