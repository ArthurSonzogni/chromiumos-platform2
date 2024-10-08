// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "dlc_list" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

const EXECUTABLE: &str = "/usr/bin/dlc_metadata_util";

const CMD: &str = "dlc_list";
const HELP: &str = "List the supported DLC(s)";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(CMD, "", HELP).set_command_callback(Some(dlc_list_callback)),
    );
}

fn dlc_list_callback(_cmd: &Command, _args: &Arguments) -> Result<(), dispatcher::Error> {
    wait_for_result(
        process::Command::new(EXECUTABLE)
            .args(vec!["--list".to_string()])
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
