// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "force_activate_fips" for turning on the FIPS mode
// for the built-in security key.

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::prompt_for_yes;

const ACTIVATE_FIPS: &str = "activate_fips";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "force_activate_fips",
            "",
            "Force activate FIPS mode for the built-in security key.",
        )
        .set_command_callback(Some(execute_activate_fips)),
    );
}

fn execute_activate_fips(_cmd: &Command, _args: &Arguments) -> Result<(), dispatcher::Error> {
    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    let flags = connection.get_u2f_flags().map_err(|err| {
        eprintln!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    let mut flags: Vec<&str> = flags.split(',').filter(|x| !x.is_empty()).collect();
    if flags.contains(&ACTIVATE_FIPS) {
        return Ok(());
    }
    flags.push(ACTIVATE_FIPS);
    let flags = flags.join(",");

    if !prompt_for_yes(
        "WARN: This might invalidate all existing credentials registered with
your built-in security key. Are you sure you want to continue?",
    ) {
        println!("Not activating FIPS mode.");
        return Ok(());
    }
    let response = connection.set_u2f_flags(&flags).map_err(|err| {
        eprintln!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    if !response.is_empty() {
        eprintln!("ERROR: {}", response);
        return Err(dispatcher::Error::CommandReturnedError);
    }

    Ok(())
}
