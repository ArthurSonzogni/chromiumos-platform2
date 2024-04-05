// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "uname" for crosh.

use std::process;

use getopts::Options;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new("uname", "[options]", "Display system info.")
            .set_command_callback(Some(execute_uptime)),
    );
}

fn execute_uptime(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    /* We keep a reduced set of options here for security */
    let mut opts = Options::new();
    opts.optflag("a", "all", "");
    opts.optflag("s", "kernel-name", "");
    opts.optflag("n", "nodename", "");
    opts.optflag("r", "kernel-release", "");
    opts.optflag("v", "kernel-version", "");
    opts.optflag("m", "machine", "");
    opts.optflag("p", "processor", "");
    opts.optflag("i", "hardware-platform", "");
    opts.optflag("o", "operating-system", "");
    opts.optflag("", "help", "");
    opts.optflag("", "version", "");

    opts.parse(args.get_tokens()).map_err(|_| {
        dispatcher::Error::CommandInvalidArguments(String::from("Unsupported option"))
    })?;

    wait_for_result(
        process::Command::new("uname")
            .args(args.get_args())
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
