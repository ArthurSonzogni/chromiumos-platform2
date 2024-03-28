// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "uptime" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "uptime".to_string(),
            "".to_string(),
            r#"Display uptime/load info.

  https://en.wikipedia.org/wiki/Uptime
  https://en.wikipedia.org/wiki/Load_(computing)"#
                .to_string(),
        )
        .set_command_callback(Some(execute_uptime)),
    );
}

fn execute_uptime(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "No argument is allowed",
        )));
    }

    wait_for_result(
        process::Command::new("uptime")
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
