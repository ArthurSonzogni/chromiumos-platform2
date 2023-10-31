// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hwsec_utils::context::RealContext;
use hwsec_utils::error::HwsecError;
use hwsec_utils::gsc::gsc_update;
use libchromeos::syslog;

static STDERR_LOGGER: StderrLogger = StderrLogger;

struct StderrLogger;

impl log::Log for StderrLogger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }
    fn log(&self, record: &log::Record) {
        eprintln!("{} - {}", record.level(), record.args());
    }
    fn flush(&self) {}
}

// This script is run at postinstall phase of Chrome OS installation process.
// It checks if the currently running cr50 image is ready to accept a
// background update and if the resident trunks_send utility is capable of
// updating the H1. If any of the checks fails, the script exits, otherwise it
// tries updating the H1 with the new cr50 image.
fn main() {
    let ident = match syslog::get_ident_from_process() {
        Some(ident) => ident,
        None => std::process::exit(1),
    };

    if let Err(e) = syslog::init(ident, true /* Log to stderr */) {
        eprintln!("failed to initialize syslog: {}", e);
        // Fallback to the std error logger.
        log::set_logger(&STDERR_LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Info);
    }

    let mut real_ctx = RealContext::new();
    match gsc_update(&mut real_ctx) {
        Ok(()) => std::process::exit(0),
        Err(hwsec_error) => match hwsec_error {
            HwsecError::GsctoolError(err_code) => std::process::exit(err_code),
            _ => std::process::exit(-1),
        },
    }
}
