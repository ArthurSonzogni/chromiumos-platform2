// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "shell" for crosh which gives developers access to bash if it is available,
// or dash otherwise.

use std::path::Path;
use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};

static DEFAULT_SHELL: &str = "/bin/sh";
static BASH_SHELL: &str = "/bin/bash";

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "shell".to_string(),
            "".to_string(),
            "Open a command line shell.".to_string(),
        )
        .set_command_callback(Some(execute_shell)),
    );
}

fn execute_shell(_cmd: &Command, _args: &Arguments) -> Result<(), dispatcher::Error> {
    println!(
        r#"Sudo commands will not succeed by default.
If you want to use sudo commands, use the VT-2 shell
(Ctrl-Alt-{{F2/Right arrow/Refresh}}) or build the image with the
login_enable_crosh_sudo USE flag:

$ USE=login_enable_crosh_sudo emerge-$BOARD chromeos-login
or
$ USE=login_enable_crosh_sudo cros build-packages --board=$BOARD
    "#
    );
    wait_for_result(
        process::Command::new(get_shell())
            .arg("-l")
            .spawn()
            .or(Err(dispatcher::Error::CommandReturnedError))?,
    )
}

fn get_shell() -> &'static str {
    if Path::new(BASH_SHELL).exists() {
        return BASH_SHELL;
    }

    DEFAULT_SHELL
}
