// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "generate_firmware_dump" for crosh through debugd.
use getopts::Options;

use crate::debugd::{Debugd, FirmwareDumpType};
use crate::dispatcher::{
    self, print_help_command_callback, Arguments, Command, Dispatcher
};

const FIRMWARE_DUMP_TYPE_ALL: &str = "all";
const FIRMWARE_DUMP_TYPE_WIFI: &str = "wifi";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "generate_firmware_dump",
            format!("<{}|{}>", FIRMWARE_DUMP_TYPE_ALL, FIRMWARE_DUMP_TYPE_WIFI),
            "Generate firmware dump for the given type.",
        )
        .set_command_callback(Some(execute_generate_firmware_dump)),
    );
}

fn execute_generate_firmware_dump(
    _cmd: &Command,
    args: &Arguments,
) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    let mut opts = Options::new();
    opts.optflag("h", "help", "print command usage");
    let matches = opts
        .parse(tokens)
        .map_err(|_| dispatcher::Error::CommandReturnedError)?;

    if matches.opt_present("h") {
        // Reuse the default command helper.
        return print_help_command_callback(_cmd, args);
    }

    if tokens.len() != 1 {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "Invalid number of arguments",
        )));
    }

    let fwdump_type = match tokens[0].as_str() {
        FIRMWARE_DUMP_TYPE_WIFI => FirmwareDumpType::Wifi,
        FIRMWARE_DUMP_TYPE_ALL => FirmwareDumpType::All,
        _ => return Err(dispatcher::Error::CommandInvalidArguments(
            "Unknown argument: ".to_string() + tokens[0].as_str(),
        )),
    };

    let success = Debugd::new()
        .map_err(|_| dispatcher::Error::CommandReturnedError)?
        .generate_firmware_dump(fwdump_type)
        .map_err(|err| {
            println!("ERROR: Got unexpected result: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

    if !success {
        println!(
            "Warning: Firmware dump generation requested, but low layer \
            execution may not be successful."
        );
    }
    Ok(())
}
