// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "upload_devcoredumps" for crosh through debugd.

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "upload_devcoredumps".to_string(),
            "<enable|disable>".to_string(),
            "Enable or disable the upload of devcoredump reports.".to_string(),
        )
        .set_command_callback(Some(execute_upload_devcoredumps)),
    );
}

fn execute_upload_devcoredumps(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if tokens.len() != 1 {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "Invalid number of arguments",
        )));
    }

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    match tokens[0].as_str() {
        "enable" => {
            connection.enable_dev_coredump_upload().map_err(|err| {
                println!("ERROR: Got unexpected result: {}", err);
                dispatcher::Error::CommandReturnedError
            })?;
            Ok(())
        }
        "disable" => {
            connection.disable_dev_coredump_upload().map_err(|err| {
                println!("ERROR: Got unexpected result: {}", err);
                dispatcher::Error::CommandReturnedError
            })?;
            Ok(())
        }
        _ => Err(dispatcher::Error::CommandInvalidArguments(
            "Unknown argument: ".to_string() + tokens[0].as_str(),
        )),
    }
}
