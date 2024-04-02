// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the `display_debug` command which can be used to assist with log collection for feedback reports.

use std::{io, thread, time};

use crate::debugd::{DRMTraceCategories, DRMTraceSize, DRMTraceSnapshotType, Debugd};
use crate::dispatcher::{self, Arguments, Command, Dispatcher};

impl DRMTraceCategories {
    fn default() -> DRMTraceCategories {
        DRMTraceCategories::empty()
    }

    fn debug() -> DRMTraceCategories {
        DRMTraceCategories::DRIVER
            | DRMTraceCategories::KMS
            | DRMTraceCategories::PRIME
            | DRMTraceCategories::ATOMIC
            | DRMTraceCategories::STATE
            | DRMTraceCategories::LEASE
            | DRMTraceCategories::DP
    }
}

const TRACE_START_LOG: &str = "DISPLAY-DEBUG-START-TRACE";
const TRACE_STOP_LOG: &str = "DISPLAY-DEBUG-STOP-TRACE";
const DIAGNOSE_START_LOG: &str = "DISPLAY-DEBUG-START-DIAGNOSE";
const DIAGNOSE_STOP_LOG: &str = "DISPLAY-DEBUG-STOP-DIAGNOSE";
const DIAGNOSE_DISPLAYS_DISCONNECTED_LOG: &str = "DISPLAY-DEBUG-DISPLAYS-DISCONNECTED";
const DIAGNOSE_DISPLAY_RECONNECTED_LOG: &str = "DISPLAY-DEBUG-DISPLAY-RECONNECTED";
const DIAGNOSE_DISPLAY_WORKING_LOG: &str = "DISPLAY-DEBUG-DISPLAY-WORKING";
const DIAGNOSE_DISPLAY_NOT_WORKING_LOG: &str = "DISPLAY-DEBUG-DISPLAY-NOT-WORKING";

const DIAGNOSE_INTRO: &str = "The 'display_debug diagnose' tool will collect \
    additional logs while walking you through a sequence of diagnostic steps. \
    Upon completion, file feedback using Alt+Shift+i.";
const DIAGNOSE_BEGIN_PROMPT: &str = "Do you want to begin now?";
const DIAGNOSE_DO_DISCONNECT: &str = "Disconnect all displays and docks from \
    the device. If you are using a Chromebox leave your main display connected.";
const DIAGNOSE_DISCONNECT_PROMPT: &str = "Press <enter> after you have \
    disconnected all displays.";
const DIAGNOSE_WAIT_STEADY_STATE: &str = "Waiting for the system to reach a steady state...";
const DIAGNOSE_RECONNECT_PROMPT: &str = "Reconnect one display. Press <enter> when you \
    are done.";
const DIAGNOSE_HAVE_MORE_DISPLAYS: &str = "Do you have any more displays to reconnect?";
const DIAGNOSE_DISPLAYS_WORKING_PROMPT: &str = "Are your displays working as expected?";
const DIAGNOSE_COMPLETE: &str = "Thanks for using the 'display_debug diagnose' tool! \
    Please file feedback using Alt+Shift+i and provide detailed information including:
    \t- Make and model of display(s)
    \t- Make and model of dock(s) in use
    \t- Make and model of dongles or converters in use";

// Represents a user's Y/N choice at a prompt.
enum Choice {
    Yes,
    No,
}

fn read_input() -> Result<String, io::Error> {
    let mut input = String::new();
    io::stdin()
        .read_line(&mut input)
        .map(|_| input.trim().to_string())
}

fn prompt_yes_no(question: &str) -> Result<Choice, io::Error> {
    loop {
        println!("{}: Y/N", question);
        let input = read_input()?;
        match input.to_lowercase().as_str() {
            "yes" | "y" => return Ok(Choice::Yes),
            "no" | "n" => return Ok(Choice::No),
            _ => {}
        }
    }
}

fn prompt_enter(prompt: &str) {
    println!("{}", prompt);
    // Read a line of input from stdin. This requires the user to have
    // pressed enter. Ignore the contents.
    let _ = read_input();
}

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "display_debug".to_string(),
            "".to_string(),
            "A tool to assist with collecting logs when reproducing display related issues."
                .to_string(),
        )
        .set_command_callback(Some(execute_display_debug))
        .register_subcommand(
            Command::new(
                "trace_start".to_string(),
                "Usage: display_debug trace_start".to_string(),
                "Increase size and verbosity of logging through drm_trace.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_trace_start)),
        )
        .register_subcommand(
            Command::new(
                "trace_stop".to_string(),
                "Usage: display_debug trace_stop".to_string(),
                "Reset size and verbosity of logging through drm_trace to defaults.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_trace_stop)),
        )
        .register_subcommand(
            Command::new(
                "trace_annotate".to_string(),
                "Usage: display_debug trace_annotate <message>".to_string(),
                "Append |message| to the drm_trace log.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_annotate)),
        )
        .register_subcommand(
            Command::new(
                "diagnose".to_string(),
                "Usage: display_debug diagnose".to_string(),
                "Give a sequence of steps to assist in debugging display issues.".to_string(),
            )
            .set_command_callback(Some(execute_display_debug_diagnose)),
        ),
    );
}

fn execute_display_debug(cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    dispatcher::print_help_command_callback(cmd, args)?;
    // Don't consider the cases where a user calls `display_debug` with no args or as
    // `display_debug help` as errors.
    match args.get_args().first().map(String::as_str) {
        Some("help") | None => Ok(()),
        Some(command) => Err(dispatcher::Error::CommandNotFound(command.to_string())),
    }
}

fn execute_display_debug_trace_start(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    println!("Increasing size and verbosity of drm_trace log. Call `display_debug trace_stop` to restore to default.");

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    do_trace_start(&connection, TRACE_START_LOG).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })
}

fn do_trace_start(connection: &Debugd, log: &str) -> Result<(), dbus::Error> {
    connection
        .drmtrace_set_size(DRMTraceSize::Debug)
        .and_then(|d| d.drmtrace_set_categories(DRMTraceCategories::debug()))
        .and_then(|d| d.drmtrace_annotate_log(String::from(log)))
        .map(|_| ())
}

fn execute_display_debug_trace_stop(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    println!("Saving drm_trace log to /var/log/display_debug/. Restoring size and verbosity of drm_trace log to default.");

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    do_trace_stop(&connection, TRACE_STOP_LOG).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })
}

fn do_trace_stop(connection: &Debugd, log: &str) -> Result<(), dbus::Error> {
    connection
        .drmtrace_annotate_log(String::from(log))
        .and_then(|d| d.drmtrace_snapshot(DRMTraceSnapshotType::Trace))
        .and_then(|d| d.drmtrace_set_categories(DRMTraceCategories::default()))
        .and_then(|d| d.drmtrace_set_size(DRMTraceSize::Default))
        .map(|_| ())
}

fn execute_display_debug_annotate(
    _cmd: &Command,
    args: &Arguments,
) -> Result<(), dispatcher::Error> {
    let tokens = args.get_args();
    if tokens.is_empty() {
        return Err(dispatcher::Error::CommandInvalidArguments(
            "missing log argument".to_string(),
        ));
    }
    let log = tokens.join(" ");

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    match connection.drmtrace_annotate_log(log) {
        Ok(_) => Ok(()),
        Err(err) => {
            println!("ERROR: Got unexpected result: {}", err);
            Err(dispatcher::Error::CommandReturnedError)
        }
    }
}

fn execute_display_debug_diagnose(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    // Explain to the user what this tool does and confirm they want to proceed.
    println!("{}", DIAGNOSE_INTRO);
    match prompt_yes_no(DIAGNOSE_BEGIN_PROMPT) {
        Ok(Choice::Yes) => (),
        Ok(Choice::No) => return Ok(()),
        Err(err) => {
            println!("Error reading input: {}", err);
            return Err(dispatcher::Error::CommandReturnedError);
        }
    }

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    // Annotate the log, expand logging categories.
    do_trace_start(&connection, DIAGNOSE_START_LOG).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    // Tell user to disconnect displays, and wait until they say they are done.
    println!("{}", DIAGNOSE_DO_DISCONNECT);
    prompt_enter(DIAGNOSE_DISCONNECT_PROMPT);

    // Wait a few seconds after user reports they are done, to wait
    // for the system to reach a steady state.
    println!("{}", DIAGNOSE_WAIT_STEADY_STATE);
    thread::sleep(time::Duration::from_secs(5));

    // Annotate the log to annotate that the displays are disconnected and take a snapshot
    // of modetest.
    if let Err(err) = connection
        .drmtrace_annotate_log(String::from(DIAGNOSE_DISPLAYS_DISCONNECTED_LOG))
        .and_then(|d| d.drmtrace_snapshot(DRMTraceSnapshotType::Modetest))
    {
        eprintln!(
            "Error invoking D-Bus method after display disconnection: {}",
            err
        )
    }

    loop {
        prompt_enter(DIAGNOSE_RECONNECT_PROMPT);

        // Wait to reach a steady state.
        println!("{}", DIAGNOSE_WAIT_STEADY_STATE);
        thread::sleep(time::Duration::from_secs(5));

        // Annotate the log that a display has been reconnected and take a snapshot of modetest.
        if let Err(err) = connection
            .drmtrace_annotate_log(String::from(DIAGNOSE_DISPLAY_RECONNECTED_LOG))
            .and_then(|d| d.drmtrace_snapshot(DRMTraceSnapshotType::Modetest))
        {
            eprintln!(
                "Error invoking D-Bus method after display reconnection: {}",
                err
            )
        }
        match prompt_yes_no(DIAGNOSE_HAVE_MORE_DISPLAYS) {
            Ok(Choice::Yes) => (),
            Ok(Choice::No) => break,
            Err(err) => {
                println!("Error reading input: {}", err);
                return Err(dispatcher::Error::CommandReturnedError);
            }
        }
    }

    // Ask the user if things are working, and log their response.
    let log = match prompt_yes_no(DIAGNOSE_DISPLAYS_WORKING_PROMPT) {
        Ok(Choice::Yes) => DIAGNOSE_DISPLAY_WORKING_LOG,
        Ok(Choice::No) => DIAGNOSE_DISPLAY_NOT_WORKING_LOG,
        Err(err) => {
            println!("Error reading input: {}", err);
            return Err(dispatcher::Error::CommandReturnedError);
        }
    };

    if let Err(err) = connection.drmtrace_annotate_log(String::from(log)) {
        eprintln!("Error annotating drm_trace log: {}", err)
    }

    // Annotate the log, restore default logging categories.
    do_trace_stop(&connection, DIAGNOSE_STOP_LOG).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    println!("{}", DIAGNOSE_COMPLETE);

    Ok(())
}
