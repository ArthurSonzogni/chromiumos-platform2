// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "vmstat" for crosh.

use std::process;

use getopts::Options;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "vmstat".to_string(),
            "[options]".to_string(),
            "Report virtual memory statistics.".to_string(),
        )
        .set_command_callback(Some(execute_uptime)),
    );
}

fn execute_uptime(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    /* We keep a reduced set of options here for security */
    let mut opts = Options::new();
    opts.optflag("a", "active", "");
    opts.optflag("f", "forks", "");
    opts.optflag("m", "slabs", "");
    opts.optflag("n", "one-header", "");
    opts.optflag("s", "stats", "");
    opts.optflag("d", "disk", "");
    opts.optflag("D", "disk-sum", "");
    // The tool only does strcmp against the partition argument.
    // It doesn't try to open or access the path directly.
    opts.optopt("p", "partition", "", "");
    opts.optopt("S", "unit", "", "");
    opts.optflag("w", "wide", "");
    opts.optflag("t", "timestamp", "");

    opts.optflag("h", "help", "");
    opts.optflag("V", "version", "");

    opts.parse(args.get_tokens()).map_err(|_| {
        dispatcher::Error::CommandInvalidArguments(String::from("Unsupported option"))
    })?;

    wait_for_result(
        process::Command::new("vmstat")
            .args(args.get_args())
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}
