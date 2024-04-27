// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "top" for crosh.

use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "top",
            "",
            r#"Run top.  This will tell you what resources are currently being used and low
  level system processes.

  Users should use the Chrome Task Monitor: press Search+Escape to open it.

  https://man7.org/linux/man-pages/man1/top.1.html
  https://gitlab.com/procps-ng/procps"#,
        )
        .set_command_callback(Some(execute_top)),
    );
}

fn execute_top(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if !tokens.is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "Invalid number of arguments",
        )));
    }

    // -s is "secure" mode, which disables kill, renice, and change display/sleep
    // interval.  Set HOME to /mnt/empty to make sure we don't parse any files in
    // the stateful partition.  https://crbug.com/677934
    wait_for_result(
        process::Command::new("top")
            .env("HOME", "/mnt/empty")
            .arg("-s")
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
