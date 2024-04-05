// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "evtest" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new("evtest", "", "Run evtest in safe mode.")
            .set_command_callback(Some(execute_evtest)),
    );
}

fn execute_evtest(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "No argument is allowed",
        )));
    }

    // --safe is "safe" mode, which will only print limited info.
    // We don't allow any other arguments now. Any attempt to enable more
    // features should go through security review first.
    wait_for_result(
        process::Command::new("evtest")
            .arg("--safe")
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
