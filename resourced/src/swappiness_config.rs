// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use dbus::nonblock::Proxy;
use dbus::nonblock::SyncConnection;
use log::error;
use tokio::sync::mpsc::unbounded_channel;
use tokio::sync::mpsc::UnboundedReceiver;
use tokio::sync::mpsc::UnboundedSender;

use crate::common::read_from_file;
use crate::common::TuneSwappiness;
use crate::dbus::DEFAULT_DBUS_TIMEOUT;

const DEFAULT_SWAPPINESS_VALUE: u32 = 60;

enum Message {
    UpdateDefaultSwappiness(u32),
    SetSwappinessState(Option<TuneSwappiness>),
}

pub struct SwappinessConfig {
    sender: UnboundedSender<Message>,
}

pub struct SwappinessConfigProxy {
    receiver: UnboundedReceiver<Message>,
}

pub fn new_swappiness_config() -> (SwappinessConfig, SwappinessConfigProxy) {
    let (sender, receiver) = unbounded_channel();
    (
        SwappinessConfig { sender },
        SwappinessConfigProxy { receiver },
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

impl SwappinessConfigProxy {
    pub async fn run_proxy(&mut self, conn: Arc<SyncConnection>) {
        let mut default_swappiness = DEFAULT_SWAPPINESS_VALUE;
        let mut tuning: Option<TuneSwappiness> = None;

        let proxy = Proxy::new(
            "org.chromium.SwapManagement",
            "/org/chromium/SwapManagement",
            DEFAULT_DBUS_TIMEOUT,
            conn,
        );

        while let Some(msg) = self.receiver.recv().await {
            let prev_swappiness = tuning.map_or(default_swappiness, |t| t.swappiness);

            match msg {
                Message::UpdateDefaultSwappiness(val) => default_swappiness = val,
                Message::SetSwappinessState(new_tuning) => tuning = new_tuning,
            }

            let new_swappiness = tuning.map_or(default_swappiness, |t| t.swappiness);
            if prev_swappiness == new_swappiness {
                continue;
            }

            const SWAPPINESS_PATH: &str = "/proc/sys/vm/swappiness";
            let system_swappiness = match read_from_file(&SWAPPINESS_PATH) {
                Ok(val) => val,
                Err(e) => {
                    error!("Failed to read swappiness {:?}", e);
                    continue;
                }
            };
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
    }
}
