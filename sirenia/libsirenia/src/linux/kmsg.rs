// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities from reading from and writing to /dev/kmsg.

use std::cell::RefCell;
use std::fmt::{Debug, Formatter};
use std::fs::File;
use std::io::{self, Read};
use std::os::unix::io::{AsRawFd, RawFd};
use std::rc::Rc;
use std::str::FromStr;

use crate::linux::events::{EventSource, Mutator, RemoveFdMutator};
use chrono::Local;
use sys_util::{
    handle_eintr,
    syslog::{Facility, Priority},
};

const MAX_KMSG: usize = 4096;

pub trait SyslogForwarder {
    fn forward(&self, data: Vec<u8>);
}

pub trait SyslogForwarderMut {
    fn forward(&mut self, data: Vec<u8>);
}

impl<R: SyslogForwarderMut> SyslogForwarder for RefCell<R> {
    fn forward(&self, data: Vec<u8>) {
        self.borrow_mut().forward(data);
    }
}

pub struct KmsgReader {
    kmsg: File,
    fwd: Rc<dyn SyslogForwarder>,
}

impl KmsgReader {
    pub fn new(kmsg_path: &str, fwd: Rc<dyn SyslogForwarder>) -> Result<Self, io::Error> {
        Ok(KmsgReader {
            kmsg: File::open(kmsg_path)?,
            fwd,
        })
    }

    fn handle_record(&mut self, data: &[u8]) {
        // Data consists of "prefix;message".
        // Prefix consists of "priority+facility,seq,timestamp_us,flags".
        // The kernel already escapes nulls and unprintable characters.
        // Ref: https://www.kernel.org/doc/Documentation/ABI/testing/dev-kmsg
        let raw = String::from_utf8_lossy(data);
        let formatted_ts: String;
        let (priority, prefix, out) = match raw.split_once(';') {
            // Always forward, even if the message cant be parsed.
            None => (Priority::Error as u8, "[???:1]", raw.as_ref()),
            Some((prefix, msg)) => {
                let parts: Vec<&str> = prefix.split(',').collect();
                if parts.len() != 4 {
                    (Priority::Error as u8, "[???:2]", raw.as_ref())
                } else {
                    let prifac = u8::from_str(parts[0]).unwrap_or(0);
                    let pri = prifac & 7;
                    let fac = prifac & (!7);
                    // Skip user messages as they've already been forwarded.
                    if fac == (Facility::User as u8) {
                        return;
                    }
                    formatted_ts = format_kernel_ts(parts[2]);
                    (pri, formatted_ts.as_str(), msg)
                }
            }
        };
        // We use the LOG_LOCAL0 syslog facility since only the kernel is
        // allowed to use LOG_KERNEL, and as far as rsyslog is concerned these
        // messages are coming from a user process. The priority is passed
        // through unchanged.
        let prifac: u8 = (Facility::Local0 as u8) & priority;
        let ts = Local::now().format("%b %d %H:%M:%S").to_string();

        // This format seems totally undocumented. It is subtly different from
        // the RFC 5424 format used for logging over TCP/UDP.
        // TODO(b/229788845): use message formatting code in sys_util::syslog
        self.fwd.forward(
            format!("<{}>{} hypervisor[0]: {} {}", prifac, ts, prefix, out)
                .as_bytes()
                .to_vec(),
        );
    }
}

impl Debug for KmsgReader {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("KmsgReader")
            .field("kmsg", &self.kmsg)
            .finish()
    }
}

impl AsRawFd for KmsgReader {
    fn as_raw_fd(&self) -> RawFd {
        self.kmsg.as_raw_fd()
    }
}

impl EventSource for KmsgReader {
    fn on_event(&mut self) -> Result<Option<Box<dyn Mutator>>, String> {
        let mut buffer: [u8; MAX_KMSG] = [0; MAX_KMSG];
        Ok(match handle_eintr!(self.kmsg.read(&mut buffer)) {
            Ok(len) => {
                self.handle_record(&buffer[..len].to_vec());
                None
            }
            // handle EPIPE: we missed some messages
            // Err(EPIPE) => self.fwd.forward("buffer[..len].to_vec());,
            Err(_) => Some(Box::new(RemoveFdMutator(self.as_raw_fd()))),
        })
    }
}

// Format a microsecond timestamp the same way as the kernel.
// See the test for examples of expected output.
fn format_kernel_ts(micros: &str) -> String {
    if micros.len() <= 6 {
        format!("[    0.{:0>6}]", micros)
    } else {
        let pt = micros.len() - 6; // Location of decimal point
        let (secs, micros) = micros.split_at(pt);
        format!("[{:>5}.{}]", secs, micros)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    #[rustfmt::skip]
    fn format_timestamp() {
        assert_eq!(format_kernel_ts("0"),             "[    0.000000]");
        assert_eq!(format_kernel_ts("1"),             "[    0.000001]");
        assert_eq!(format_kernel_ts("123"),           "[    0.000123]");
        assert_eq!(format_kernel_ts("123456"),        "[    0.123456]");
        assert_eq!(format_kernel_ts("1234567"),       "[    1.234567]");
        assert_eq!(format_kernel_ts("123456789"),     "[  123.456789]");
        assert_eq!(format_kernel_ts("123456123456"), "[123456.123456]");
    }
}
