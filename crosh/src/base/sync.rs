// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "sync" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "sync".to_string(),
            "".to_string(),
            "Synchronize cached writes to persistent storage.".to_string(),
        )
        .set_command_callback(Some(execute_sync)),
    );
}

fn execute_sync(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "No argument is allowed",
        )));
    }

    wait_for_result(
        process::Command::new("sync")
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
