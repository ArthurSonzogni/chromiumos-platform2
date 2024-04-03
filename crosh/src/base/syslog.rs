// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "syslog" for crosh.

use log::error;
use nix::unistd::getpid;
use syslog::{Facility, Formatter3164};

use crate::dispatcher::{self, Arguments, Command, Dispatcher};

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "syslog",
            "<message>",
            r#"Logs a message to syslog (the system log daemon).

  This can help mark/flag/label the logs just before & after running a different
  operation so you can locate the relevant errors faster."#,
        )
        .set_command_callback(Some(execute_syslog)),
    );
}

fn execute_syslog(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    // The log crate doesn't support NOTICE levels, so use syslog directly.
    // https://github.com/rust-lang/log/issues/334
    let formatter = Formatter3164 {
        facility: Facility::LOG_USER,
        hostname: None,
        process: "crosh".to_string(),
        pid: getpid().as_raw() as u32,
    };
    let mut writer = syslog::unix(formatter).map_err(|err| {
        error!("unable to connect to syslog: {:?}", err);
        dispatcher::Error::CommandReturnedError
    })?;
    writer.notice(args.get_args().join(" ")).map_err(|err| {
        error!("unable to write to syslog: {:?}", err);
        dispatcher::Error::CommandReturnedError
    })
}
