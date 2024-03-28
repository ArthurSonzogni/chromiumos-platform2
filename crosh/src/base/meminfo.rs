// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "meminfo" for crosh.

use std::fs::read_to_string;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};

const MEMINFO: &str = "/proc/meminfo";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "meminfo".to_string(),
            "".to_string(),
            "Display detailed memory statistics.".to_string(),
        )
        .set_command_callback(Some(execute_meminfo)),
    );
}

fn execute_meminfo(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "No argument is allowed",
        )));
    }

    let contents = read_to_string(MEMINFO).map_err(|err| {
        eprintln!("{}: {}", MEMINFO, err);
        dispatcher::Error::CommandReturnedError
    })?;
    print!("{}", contents);
    Ok(())
}
