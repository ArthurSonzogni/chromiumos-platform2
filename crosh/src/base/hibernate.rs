// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "hibernate" for crosh which tries to hibernate the
// system (if hibernate is available and enabled).

use dbus::blocking::Connection;
use libchromeos::sys::error;
use system_api::client::OrgChromiumPowerManager;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "hibernate".to_string(),
            "".to_string(),
            r#"Attempt to hibernate the system. Only available on devices for which
  hibernate is enabled."#
                .to_string(),
        )
        .set_command_callback(Some(hibernate)),
    );
}

fn hibernate(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(
            "too many arguments".to_string(),
        ));
    }

    let connection = Connection::new_system().map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    let conn_path = connection.with_proxy(
        "org.chromium.PowerManager",
        "/org/chromium/PowerManager",
        DEFAULT_DBUS_TIMEOUT,
    );

    conn_path.request_suspend(0, 0, 2).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    Ok(())
}
