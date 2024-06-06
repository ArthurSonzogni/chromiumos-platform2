// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::sync::{Arc, Mutex};

use log::{Level, LevelFilter, Log};

const FLEXOR_TAG: &str = "flexor";
const DEFAULT_DEVICE: &str = "/dev/kmsg";
const MAX_LEVEL: LevelFilter = LevelFilter::Info;

// A logger that logs to the kernel message buffer.
struct KernelLogger {
    file: Arc<Mutex<File>>,
}

impl KernelLogger {
    fn new() -> io::Result<Self> {
        Ok(KernelLogger {
            file: Arc::new(Mutex::new(
                OpenOptions::new().write(true).open(DEFAULT_DEVICE)?,
            )),
        })
    }
}

impl Log for KernelLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= MAX_LEVEL
    }

    fn log(&self, record: &log::Record) {
        if record.level() > MAX_LEVEL {
            return;
        }

        let level: u8 = match record.level() {
            Level::Error => 3,
            Level::Warn => 4,
            Level::Info => 5,
            Level::Debug => 6,
            Level::Trace => 7,
        };

        let mut buf = Vec::new();
        _ = writeln!(
            &mut buf,
            "<{level}>{FLEXOR_TAG}[{}]: {}",
            unsafe { nix::libc::getpid() },
            record.args()
        );

        if let Ok(mut kmsg) = self.file.lock() {
            _ = kmsg.write(&buf);
            _ = kmsg.flush();
        }
    }

    fn flush(&self) {}
}

pub fn init() -> anyhow::Result<()> {
    let klog = KernelLogger::new()?;
    log::set_boxed_logger(Box::new(klog))?;
    log::set_max_level(MAX_LEVEL);

    Ok(())
}