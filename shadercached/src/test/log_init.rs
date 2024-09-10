// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::LazyLock;

// b/365738717: The prior implementation of logging init used `ctor::ctor` to install a
// `stderrlog` instance. This init was unsequenced WRT the init of Rust's runtime, which caused
// crashes.
//
// This wrapper exists so we can globally set the logger in a ctor, but defer its initialization
// until the logger's first use.
struct LazyStderrLogger {
    logger: LazyLock<stderrlog::StdErrLog>,
}

impl log::Log for LazyStderrLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        self.logger.enabled(metadata)
    }

    fn log(&self, record: &log::Record<'_>) {
        self.logger.log(record)
    }

    fn flush(&self) {
        self.logger.flush()
    }
}

static LAZY_STDERR_LOGGER: LazyStderrLogger = LazyStderrLogger {
    logger: LazyLock::new(|| {
        let mut r = stderrlog::new();
        r.verbosity(log::Level::Debug);
        r
    }),
};

/// Install a test logger. Panics if a logger has already been installed.
pub fn init() {
    log::set_logger(&LAZY_STDERR_LOGGER).expect("Logger was set already?")
}
