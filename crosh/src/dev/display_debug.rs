// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the `display_debug` command which can be used to assist with log collection for feedback reports.

use dbus::blocking::Connection;
use system_api::client::OrgChromiumDebugd;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

struct Debugd {
    connection: dbus::blocking::Connection,
}

impl Debugd {
    fn new() -> Result<Debugd, dbus::Error> {
        match Connection::new_system() {
            Ok(connection) => Ok(Debugd { connection }),
            Err(err) => Err(err),
        }
    }

    fn drmtrace_annotate_log(self, log: String) -> Result<Debugd, dbus::Error> {
        self.connection
            .with_proxy(
                "org.chromium.debugd",
                "/org/chromium/debugd",
                DEFAULT_DBUS_TIMEOUT,
            )
            .drmtrace_annotate_log(&log)
            .map(|_| self)
    }
}

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "display_debug".to_string(),
            "".to_string(),
            "A tool to assist with collecting logs when reproducing display related issues."
                .to_string(),
        )
        .set_command_callback(Some(execute_display_debug))
        .register_subcommand(
            Command::new(
                "trace_start".to_string(),
                "Usage: display_debug trace_start".to_string(),
                "Increase size and verbosity of logging through drm_trace.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_trace_start)),
        )
        .register_subcommand(
            Command::new(
                "trace_stop".to_string(),
                "Usage: display_debug trace_stop".to_string(),
                "Reset size and verbosity of logging through drm_trace to defaults.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_trace_stop)),
        )
        .register_subcommand(
            Command::new(
                "trace_annotate".to_string(),
                "Usage: display_debug trace_annotate <message>".to_string(),
                "Append |message| to the drm_trace log.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_annotate)),
        ),
    );
}

fn execute_display_debug(cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    dispatcher::print_help_command_callback(cmd, args)
}

fn execute_display_debug_trace_start(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    unimplemented!();
}

fn execute_display_debug_trace_stop(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    unimplemented!();
}

fn execute_display_debug_annotate(
    _cmd: &Command,
    args: &Arguments,
) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if tokens.is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(
            "missing log argument".to_string(),
        ));
    }
    let log = tokens.join(" ");

    match Debugd::new().and_then(|d| d.drmtrace_annotate_log(log)) {
        Ok(_) => Ok(()),
        Err(err) => {
            println!("ERROR: Got unexpected result: {}", err);
            Err(dispatcher::Error::CommandReturnedError)
        }
    }
}
