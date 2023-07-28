// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the `printscan_debug` command which can be used to assist with log
// collection for feedback reports.

use dbus::blocking::Connection;
use std::error::Error;
use system_api::client::OrgChromiumPrintscanmgr;
use system_api::printscanmgr_service::{
    printscan_debug_set_categories_request::DebugLogCategory, PrintscanDebugSetCategoriesRequest,
};

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

struct Printscanmgr {
    connection: dbus::blocking::Connection,
}

impl Printscanmgr {
    fn new() -> Result<Printscanmgr, dbus::Error> {
        Connection::new_system().map(|connection| Printscanmgr { connection })
    }

    fn printscan_debug_set_categories(
        self,
        request: PrintscanDebugSetCategoriesRequest,
    ) -> Result<Printscanmgr, Box<dyn Error>> {
        let request_bytes = protobuf::Message::write_to_bytes(&request)?;
        Ok(self
            .connection
            .with_proxy(
                "org.chromium.printscanmgr",
                "/org/chromium/printscanmgr",
                DEFAULT_DBUS_TIMEOUT,
            )
            .printscan_debug_set_categories(request_bytes)
            .map(|_| self)?)
    }
}

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "printscan_debug".to_string(),
            "".to_string(),
            "A tool to assist with collecting logs when reproducing printing and \
            scanning related issues."
                .to_string(),
        )
        .set_command_callback(Some(execute_printscan_debug))
        .register_subcommand(
            Command::new(
                "start".to_string(),
                "Usage: printscan_debug start [all|scanning|printing]".to_string(),
                "Start collecting printscan debug logs.".to_string(),
            )
            .set_command_callback(Some(execute_printscan_debug_start)),
        )
        .register_subcommand(
            Command::new(
                "stop".to_string(),
                "Usage: printscan_debug stop".to_string(),
                "Stop collecting printscan debug logs.".to_string(),
            )
            .set_command_callback(Some(execute_printscan_debug_stop)),
        ),
    );
}

fn execute_printscan_debug(cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    dispatcher::print_help_command_callback(cmd, args)
}

fn execute_printscan_debug_start(
    _cmd: &Command,
    args: &Arguments,
) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    // TODO(b/259452698) Extend to support combinations of tokens.
    if tokens.len() != 1 {
        return Err(dispatcher::Error::CommandInvalidArguments(
            "Invalid number of arguments".to_string(),
        ));
    }
    let categories = match tokens[0].as_str() {
        "all" => vec![
            DebugLogCategory::DEBUG_LOG_CATEGORY_PRINTING,
            DebugLogCategory::DEBUG_LOG_CATEGORY_SCANNING,
        ],
        "printing" => vec![DebugLogCategory::DEBUG_LOG_CATEGORY_PRINTING],
        "scanning" => vec![DebugLogCategory::DEBUG_LOG_CATEGORY_SCANNING],
        _ => {
            return Err(dispatcher::Error::CommandInvalidArguments(
                "Invalid category: ".to_string() + tokens[0].as_str(),
            ))
        }
    };
    println!(
        "Warning: Advanced logging has been enabled for printing and scanning tasks. Take \
             caution when printing or scanning sensitive documents as printed or scanned \
             documents may be saved to your device and included in feedback reports. This \
             advanced logging can be disabled by running `printscan_debug stop`, logging out, \
             or rebooting your device."
    );
    let mut request = PrintscanDebugSetCategoriesRequest::new();
    for category in &categories {
        request.categories.push((*category).into());
    }
    request.disable_logging = false;
    Printscanmgr::new()
        .map(|p| p.printscan_debug_set_categories(request))
        .map(|_| ())
        .map_err(|err| {
            println!(
                "ERROR: Got unexpected result: {}. Please reboot your system and try again.",
                err
            );
            dispatcher::Error::CommandReturnedError
        })
}

fn execute_printscan_debug_stop(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if !tokens.is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(
            "Too many arguments".to_string(),
        ));
    }
    println!("Done collecting printscan debug logs.");
    let mut request = PrintscanDebugSetCategoriesRequest::new();
    request.disable_logging = true;
    Printscanmgr::new()
        .map(|p| p.printscan_debug_set_categories(request))
        .map(|_| ())
        .map_err(|err| {
            println!(
                "ERROR: Got unexpected result: {}. Logging has not been disabled. \
                Please reboot your system.",
                err
            );
            dispatcher::Error::CommandReturnedError
        })
}
