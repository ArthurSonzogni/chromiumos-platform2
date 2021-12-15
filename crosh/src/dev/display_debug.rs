// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the `display_debug` command which can be used to assist with log collection for feedback reports.

use bitflags::bitflags;
use dbus::blocking::Connection;
use system_api::client::OrgChromiumDebugd;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

// These bitflag values must match those in org.chromium.debugd.xml.
bitflags! {
    struct DRMTraceCategories: u32 {
        const CORE =    0x001;
        const DRIVER =  0x002;
        const KMS =     0x004;
        const PRIME =   0x008;
        const ATOMIC =  0x010;
        const VBL =     0x020;
        const STATE =   0x040;
        const LEASE =   0x080;
        const DP =      0x100;
        const DRMRES =  0x200;
    }
}

impl DRMTraceCategories {
    fn default() -> DRMTraceCategories {
        DRMTraceCategories { bits: 0 }
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

// These enum values must match those in org.chromium.debugd.xml.
enum DRMTraceSize {
    Default = 0,
    Debug = 1,
}

// These enum values must match those in org.chromium.debugd.xml.
enum DRMTraceSnapshotType {
    Trace = 0,
}

const TRACE_START_LOG: &str = "DISPLAY-DEBUG-START-TRACE";
const TRACE_STOP_LOG: &str = "DISPLAY-DEBUG-STOP-TRACE";

struct Debugd {
    connection: dbus::blocking::Connection,
}

impl Debugd {
    fn new() -> Result<Debugd, dbus::Error> {
        match Connection::new_system() {
            Ok(connection) => Ok(Debugd { connection }),
            Err(err) => Err(err),
        }
    }

    fn drmtrace_annotate_log(self, log: String) -> Result<Debugd, dbus::Error> {
        self.connection
            .with_proxy(
                "org.chromium.debugd",
                "/org/chromium/debugd",
                DEFAULT_DBUS_TIMEOUT,
            )
            .drmtrace_annotate_log(&log)
            .map(|_| self)
    }

    fn drmtrace_snapshot(self, snapshot_type: DRMTraceSnapshotType) -> Result<Debugd, dbus::Error> {
        self.connection
            .with_proxy(
                "org.chromium.debugd",
                "/org/chromium/debugd",
                DEFAULT_DBUS_TIMEOUT,
            )
            .drmtrace_snapshot(snapshot_type as u32)
            .map(|_| self)
    }

    fn drmtrace_set_size(self, size: DRMTraceSize) -> Result<Debugd, dbus::Error> {
        self.connection
            .with_proxy(
                "org.chromium.debugd",
                "/org/chromium/debugd",
                DEFAULT_DBUS_TIMEOUT,
            )
            .drmtrace_set_size(size as u32)
            .map(|_| self)
    }

    fn drmtrace_set_categories(
        self,
        categories: DRMTraceCategories,
    ) -> Result<Debugd, dbus::Error> {
        self.connection
            .with_proxy(
                "org.chromium.debugd",
                "/org/chromium/debugd",
                DEFAULT_DBUS_TIMEOUT,
            )
            .drmtrace_set_categories(categories.bits())
            .map(|_| self)
    }
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
        ),
    );
}

fn execute_display_debug(cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    dispatcher::print_help_command_callback(cmd, args)
}

fn execute_display_debug_trace_start(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    println!("Increasing size and verbosity of drm_trace log. Call `display_debug trace_stop` to restore to default.");
    match Debugd::new()
        .and_then(|d| d.drmtrace_set_size(DRMTraceSize::Debug))
        .and_then(|d| d.drmtrace_set_categories(DRMTraceCategories::debug()))
        .and_then(|d| d.drmtrace_annotate_log(String::from(TRACE_START_LOG)))
    {
        Ok(_) => Ok(()),
        Err(err) => {
            println!("ERROR: Got unexpected result: {}", err);
            Err(dispatcher::Error::CommandReturnedError)
        }
    }
}

fn execute_display_debug_trace_stop(
    _cmd: &Command,
    _args: &Arguments,
) -> Result<(), dispatcher::Error> {
    println!("Saving drm_trace log to /var/log/display_debug/. Restoring size and verbosity of drm_trace log to default.");
    match Debugd::new()
        .and_then(|d| d.drmtrace_annotate_log(String::from(TRACE_STOP_LOG)))
        .and_then(|d| d.drmtrace_snapshot(DRMTraceSnapshotType::Trace))
        .and_then(|d| d.drmtrace_set_categories(DRMTraceCategories::default()))
        .and_then(|d| d.drmtrace_set_size(DRMTraceSize::Default))
    {
        Ok(_) => Ok(()),
        Err(err) => {
            println!("ERROR: Got unexpected result: {}", err);
            Err(dispatcher::Error::CommandReturnedError)
        }
    }
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

    match Debugd::new().and_then(|d| d.drmtrace_annotate_log(log)) {
        Ok(_) => Ok(()),
        Err(err) => {
            println!("ERROR: Got unexpected result: {}", err);
            Err(dispatcher::Error::CommandReturnedError)
        }
    }
}
