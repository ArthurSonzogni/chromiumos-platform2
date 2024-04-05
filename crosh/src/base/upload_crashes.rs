// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "upload_crashes" for crosh through debugd.

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "upload_crashes",
            "",
            r#"Uploads available crash reports to the crash server.

  Check chrome://crashes for processed crashes."#,
        )
        .set_command_callback(Some(execute_upload_crashes)),
    );
}

fn execute_upload_crashes(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if !args.get_args().is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "No argument is allowed",
        )));
    }

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    connection.upload_crashes().map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;
    println!("Check chrome://crashes for status updates.");
    Ok(())
}
