// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Logger intended to mostly mimic how the shell script logging works.
//!
//! We don't use `libchromeos::syslog` because the script didn't, and things
//! that run the script, like os_install_service, expect basic stdout/stderr.

use log::{LevelFilter, Metadata, Record, SetLoggerError};

/// Basic logger to stdout/stderr.
pub struct Logger {
    pub level: LevelFilter,
}

impl log::Log for Logger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= self.level
    }

    fn log(&self, record: &Record) {
        if !self.enabled(record.metadata()) {
            return;
        }

        let msg = format!("{}: {}", record.level(), record.args());
        if record.metadata().level() == LevelFilter::Error {
            eprintln!("{}", msg);
        } else {
            println!("{}", msg);
        }
    }

    fn flush(&self) {}
}

/// Set up logging.
pub fn init(debug: bool) -> Result<(), SetLoggerError> {
    let level = if debug {
        LevelFilter::Debug
    } else {
        LevelFilter::Info
    };
    let logger = Logger { level };
    log::set_boxed_logger(Box::new(logger)).map(|()| log::set_max_level(level))
}
