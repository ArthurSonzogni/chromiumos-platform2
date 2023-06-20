// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod dbus_constants;

use std::{future::Future, pin::Pin, sync::Arc, time::Duration};

use dbus::{
    arg::{AppendAll, ReadAll},
    channel::Sender,
    nonblock::SyncConnection,
    Message,
};

#[cfg(test)]
use mockall::{automock, predicate::*};

const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

#[cfg_attr(test, automock)]
pub trait DbusConnectionTrait {
    fn call_dbus_method<T: ReadAll + 'static, A: AppendAll + 'static>(
        &self,
        service: &str,
        path: &str,
        interface: &str,
        method: &str,
        method_args: A,
    ) -> Pin<Box<dyn Future<Output = Result<T, dbus::Error>> + Send + 'static>>;

    fn send(&self, msg: Message) -> Result<u32, ()>;
}

pub struct DbusConnection {
    conn: Arc<SyncConnection>,
}

impl DbusConnection {
    pub fn new(conn: Arc<SyncConnection>) -> Arc<DbusConnection> {
        Arc::new(DbusConnection { conn })
    }
}

impl DbusConnectionTrait for DbusConnection {
    fn call_dbus_method<T: ReadAll + 'static, A: AppendAll + 'static>(
        &self,
        service: &str,
        path: &str,
        interface: &str,
        method: &str,
        method_args: A,
    ) -> Pin<Box<dyn Future<Output = Result<T, dbus::Error>> + Send + 'static>> {
        let proxy =
            dbus::nonblock::Proxy::new(service, path, DEFAULT_DBUS_TIMEOUT, self.conn.clone());
        Box::pin(proxy.method_call(interface, method, method_args))
    }

    fn send(&self, msg: Message) -> Result<u32, ()> {
        self.conn.send(msg)
    }
}
