// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "free" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new("free", "[options]", "Display free/used memory info.")
            .set_command_callback(Some(execute_free)),
    );
}

fn execute_free(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    wait_for_result(
        process::Command::new("free")
            .args(args.get_args())
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
