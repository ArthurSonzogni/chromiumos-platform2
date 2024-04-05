// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the help string for the crosh "help_advanced" command.
//
// The actual command is handled by `main.rs`.

use crate::dispatcher::{Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(Command::new(
        "help_advanced",
        "",
        "Display the help for more advanced commands, mainly used for debugging.",
    ));
}
