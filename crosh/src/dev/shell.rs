// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "shell" for crosh which gives developers access to bash if it is available,
// or dash otherwise.

use std::os::unix::io::IntoRawFd;
use std::path::Path;
use std::process;

use crate::dispatcher::{self, wait_for_result, Arguments, Command, Dispatcher};
use crate::util::{add_epoll_for_fd, epoll_wait, is_no_new_privs_set, DEFAULT_DBUS_TIMEOUT};
use dbus::arg::OwnedFd;
use dbus::blocking::Connection;
use libc::dup;
use libchromeos::pipe;
use system_api::client::OrgChromiumDebugd;

static DEFAULT_SHELL: &str = "/bin/sh";
static BASH_SHELL: &str = "/bin/bash";
static ISOLATED_SHELL: &str = "--isolated";

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

fn execute_shell(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if tokens.len() > 1 {
        eprintln!("too many arguments");
        return Err(dispatcher::Error::CommandReturnedError);
    }

    if tokens.contains(&ISOLATED_SHELL.to_owned()) {
        // Set up D-Bus connection for creating a shell in separate process tree.
        let connection = Connection::new_system().map_err(|err| {
            eprintln!("ERROR: Failed to get D-Bus connection: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;
        let conn_path = connection.with_proxy(
            "org.chromium.debugd",
            "/org/chromium/debugd",
            DEFAULT_DBUS_TIMEOUT,
        );
        let (lifeline_read_pipe, lifeline_write_pipe) = pipe(true).unwrap();
        // TODO(315342353): use this FD pair to prevent zombie shell processes.
        let (_, caller_write_pipe) = pipe(true).unwrap();
        conn_path
            .crosh_shell_start(
                // Safe because the lifeline_write_pipe isn't copied elsewhere.
                unsafe { OwnedFd::new(lifeline_write_pipe.into_raw_fd()) },
                // Safe because the caller_write_pipe isn't copied elsewhere.
                unsafe { OwnedFd::new(caller_write_pipe.into_raw_fd()) },
                // Safe because this will always be the STDIN file descriptor.
                unsafe { OwnedFd::new(dup(libc::STDIN_FILENO)) },
                // Safe because this will always be the STDOUT file descriptor.
                unsafe { OwnedFd::new(dup(libc::STDOUT_FILENO)) },
            )
            .map_err(|err| {
                eprintln!("ERROR: Got unexpected result: {}", err);
                dispatcher::Error::CommandReturnedError
            })?;
        let epoll_fd = add_epoll_for_fd(lifeline_read_pipe.into_raw_fd()).map_err(|err| {
            eprintln!("ERROR: Epoll error: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

        // Loop until we see an epoll event indicating the shell has closed.
        // We need to keep calling epoll_wait() because the syscall will also
        // return if it's interrupted such as by SIGINT.
        loop {
            let event_count = epoll_wait(epoll_fd);
            if event_count > 0 {
                return Ok(());
            }
        }
    } else {
        if is_no_new_privs_set() {
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
        }
        wait_for_result(
            process::Command::new(get_shell())
                .arg("-l")
                .spawn()
                .or(Err(dispatcher::Error::CommandReturnedError))?,
        )
    }
}
fn get_shell() -> &'static str {
    if Path::new(BASH_SHELL).exists() {
        return BASH_SHELL;
    }

    DEFAULT_SHELL
}
