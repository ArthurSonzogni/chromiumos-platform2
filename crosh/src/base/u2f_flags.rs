// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "u2f_flags" for crosh through debugd.

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};

const WARNING: &str = r#"### IMPORTANT: The U2F feature is experimental and not suitable for
### general production use in its current form. The current
### implementation is still in flux and some features (including
### security-relevant ones) are still missing. You are welcome to
### play with this, but use at your own risk. You have been warned."#;

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "u2f_flags",
            "<u2f | g2f>[, enable_global_key, verbose]",
            r#"Set flags to override the second-factor authentication daemon configuration.
  u2f: Always enable the standard U2F mode even if not set in device policy.
  g2f: Always enable the U2F mode plus some additional extensions.
  enable_global_key: Make the power button security key "global" - can be used outside a
                     logged-in session for the google.com relying party.
  verbose: Increase the daemon logging verbosity in /var/log/messages."#,
        )
        .set_command_callback(Some(execute_u2f_flags)),
    );
}

fn execute_u2f_flags(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if tokens.len() != 1 {
        return Err(dispatcher::Error::CommandInvalidArguments(String::from(
            "Invalid number of arguments",
        )));
    }

    println!("{}", WARNING);

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    connection
        .set_u2f_flags(tokens[0].as_str())
        .map_err(|err| {
            eprintln!("ERROR: Got unexpected result: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;
    Ok(())
}
