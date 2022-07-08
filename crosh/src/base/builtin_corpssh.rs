// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "builtin_corpssh" for crosh which enables/disables
// built-in gnubby's CorpSSH support.

use dbus::blocking::Connection;
use system_api::client::OrgChromiumDebugd;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

const INVALID_ARGUMENTS_MSG: &str = "Please pass in a single argument 'enable' or 'disable'.";
const CORP_PROTOCOL: &str = "corp_protocol";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "builtin_corpssh".to_string(),
            "<enable|disable>".to_string(),
            "Enable or disable CorpSSH support for the built-in gnubby.".to_string(),
        )
        .set_command_callback(Some(execute_builtin_corpssh)),
    );
}

fn execute_builtin_corpssh(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let args = args.get_args();
    if args.len() != 1 {
        return Err(dispatcher::Error::CommandInvalidArguments(
            INVALID_ARGUMENTS_MSG.to_string(),
        ));
    }

    let enabled = match args[0].as_str() {
        "enable" => true,
        "disable" => false,
        _ => {
            return Err(dispatcher::Error::CommandInvalidArguments(
                INVALID_ARGUMENTS_MSG.to_string(),
            ))
        }
    };

    let connection = Connection::new_system().map_err(|err| {
        eprintln!("ERROR: Failed to get D-Bus connection: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;
    let conn_path = connection.with_proxy(
        "org.chromium.debugd",
        "/org/chromium/debugd",
        DEFAULT_DBUS_TIMEOUT,
    );

    let flags = conn_path.get_u2f_flags().map_err(|err| {
        eprintln!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    let mut flags: Vec<&str> = flags.split(",").collect();
    flags.retain(|&flag| flag != CORP_PROTOCOL);
    if enabled {
        flags.push(CORP_PROTOCOL);
    }

    let flags = flags.join(",");
    let response = conn_path.set_u2f_flags(&flags).map_err(|err| {
        eprintln!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    if !response.is_empty() {
        eprintln!("ERROR: {}", response);
        return Err(dispatcher::Error::CommandReturnedError);
    }

    Ok(())
}
