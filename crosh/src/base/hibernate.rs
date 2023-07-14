// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "hibernate" for crosh which tries to hibernate the
// system (if hibernate is available and enabled).

use dbus::blocking::Connection;
use libchromeos::sys::error;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

const SUSPEND_FLAVOR_HIBERNATE: u32 = 2;

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

    let proxy = connection.with_proxy(
        "org.chromium.PowerManager",
        "/org/chromium/PowerManager",
        DEFAULT_DBUS_TIMEOUT,
    );

    proxy
        .method_call(
            "org.chromium.PowerManager",
            "RequestSuspend",
            (SUSPEND_FLAVOR_HIBERNATE,),
        )
        .map_err(|err| {
            error!("ERROR: D-Bus method call failed: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

    Ok(())
}
