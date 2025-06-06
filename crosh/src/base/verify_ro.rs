// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "verify_ro" for crosh which checks the ro of Cr50 firmware.

use std::fs::metadata;
use std::io::{copy, stdout};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::{sleep, spawn};
use std::time::Duration;

use libc::c_int;
use libchromeos::pipe;
use nix::sys::signal::Signal;

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::{clear_signal_handlers, set_signal_handlers};

const BINARY: &str = "/usr/sbin/gsc_verify_ro";

pub fn register(dispatcher: &mut Dispatcher) {
    // Only register the verify_ro command if the binary is present.
    if !Path::new(BINARY).exists() {
        return;
    }
    dispatcher.register_command(
        Command::new(
            "verify_ro",
            "",
            "Verify AP and EC RO firmware on a ChromeOS device connected over SuzyQ
  cable, if supported by the device.",
        )
        .set_command_callback(Some(execute_verify_ro)),
    );
}

fn stop_verify_ro(connection: &Debugd, handle: &str) -> Result<(), dispatcher::Error> {
    connection
        .update_and_verify_fwon_usb_stop(handle)
        .map_err(|err| {
            println!("ERROR: Got unexpected result: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

    Ok(())
}

// Set to true when SIGINT is received and triggers sending a stop command over D-Bus.
static STOP_FLAG: AtomicBool = AtomicBool::new(false);
// Set to true when the original D-Bus command closes the pipe signalling completion.
static DONE_FLAG: AtomicBool = AtomicBool::new(false);

// Handle Ctrl-c/SIGINT by sending a stop over D-Bus.
extern "C" fn sigint_handler(_: c_int) {
    STOP_FLAG.store(true, Ordering::Release);
}

fn execute_verify_ro(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    if args.get_tokens().len() != 1 {
        eprintln!("too many arguments");
        return Err(dispatcher::Error::CommandReturnedError);
    }

    const CR50_IMAGE: &str = "/opt/google/cr50/firmware/cr50.bin.prod";
    const RO_DB: &str = "/opt/google/cr50/ro_db";
    if match metadata(CR50_IMAGE) {
        Ok(data) => !data.is_file(),
        _ => true,
    } && match metadata(RO_DB) {
        Ok(data) => !data.is_dir(),
        _ => true,
    } {
        eprintln!("This device can not be used for RO verification");
        return Err(dispatcher::Error::CommandReturnedError);
    }

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    // Safe because sigint_handler is async-signal safe.
    unsafe { set_signal_handlers(&[Signal::SIGINT], sigint_handler) }
    // Pass a pipe through D-Bus to collect the response.
    let (mut read_pipe, write_pipe) = pipe(true).unwrap();
    let handle = connection
        .update_and_verify_fwon_usb_start(write_pipe.into(), CR50_IMAGE, RO_DB)
        .map_err(|err| {
            println!("ERROR: Got unexpected result: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

    // Start a thread to send a stop on SIGINT, or stops when DONE_FLAG is set.
    let watcher = spawn(move || loop {
        if STOP_FLAG.load(Ordering::Acquire) {
            stop_verify_ro(&connection, &handle).unwrap_or(());
            break;
        }
        if DONE_FLAG.load(Ordering::Acquire) {
            break;
        }
        sleep(Duration::from_millis(50));
    });

    // Print the response.
    copy(&mut read_pipe, &mut stdout()).map_err(|_| dispatcher::Error::CommandReturnedError)?;

    clear_signal_handlers(&[Signal::SIGINT]);
    DONE_FLAG.store(true, Ordering::Release);
    watcher
        .join()
        .map_err(|_| dispatcher::Error::CommandReturnedError)?;

    Ok(())
}
