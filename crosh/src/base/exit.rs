// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the help string for the crosh "exit" command.
//
// The actual command is handled by the input loop in `main.rs`.
//
// If we defined a unique dispatcher Error like "Exit", we could have this
// command return that an the input loop break on it.  This would allow this
// command to do more parsing (like arguments) and other commands to exit,
// but it's not clear how much value that would bring currently.

use crate::dispatcher::{Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(Command::new(
        "exit".to_string(),
        "".to_string(),
        "Exit crosh.".to_string(),
    ));
}
