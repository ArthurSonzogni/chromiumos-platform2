// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Pressure stall information (PSI) utilities.
//!
//! PSI documentation: https://docs.kernel.org/accounting/psi.html

use std::fs::File;
use std::fs::OpenOptions;
use std::io;
use std::io::Write;
use std::time::Duration;

use tokio::io::unix::AsyncFd;
use tokio::io::Interest;

/// The target of the PSI. Either "some" or "full".
pub enum Target {
    Some,
    Full,
}

impl Target {
    fn to_str(&self) -> &'static str {
        match self {
            Target::Some => "some",
            Target::Full => "full",
        }
    }
}

/// A watcher for PSI events.
pub struct PsiWatcher {
    fd: AsyncFd<File>,
}

impl PsiWatcher {
    /// Creates a new PSI watcher for memory pressure.
    pub fn new_memory_pressure(
        target: Target,
        stall: Duration,
        window: Duration,
    ) -> io::Result<Self> {
        Self::new("/proc/pressure/memory", target, stall, window)
    }

    fn new(path: &str, target: Target, stall: Duration, window: Duration) -> io::Result<Self> {
        let mut file = OpenOptions::new().read(true).write(true).open(path)?;

        let monitor_config = format!(
            "{} {} {}\0",
            target.to_str(),
            stall.as_micros(),
            window.as_micros()
        );

        file.write_all(monitor_config.as_bytes())?;

        // Monitor POLLPRI for PSI events.
        // https://docs.kernel.org/accounting/psi.html#userspace-monitor-usage-example
        let fd = AsyncFd::with_interest(file, Interest::PRIORITY)?;

        Ok(Self { fd })
    }

    /// Waits for a PSI event to occur.
    pub async fn wait(&mut self) -> io::Result<()> {
        self.fd.readable().await?.clear_ready();
        Ok(())
    }
}
