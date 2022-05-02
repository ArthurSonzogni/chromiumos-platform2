// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities from reading from and writing to /dev/kmsg.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::fmt;
use std::fmt::{Debug, Formatter};
use std::fs::{File, OpenOptions};
use std::io::{self, Read};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, RawFd};
use std::rc::Rc;
use std::str::FromStr;

use crate::linux::events::{EventSource, Mutator, RemoveFdMutator};
use anyhow::Result;
use chrono::Local;
use sys_util::{
    handle_eintr,
    syslog::{Facility, Priority},
};

pub const KMSG_PATH: &str = "/dev/kmsg";
const MAX_KMSG_RECORD: usize = 4096;

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
        let raw = String::from_utf8_lossy(data);
        let rec = KmsgRecord::from(raw.as_ref());
        let priority = match &rec {
            KmsgRecord::NoPrefix(_) => Priority::Error as u8,
            KmsgRecord::BadPrefix(_, _) => Priority::Error as u8,
            KmsgRecord::Valid(prefix, _) => {
                let prifac = u8::from_str(prefix.prifac).unwrap_or(0);
                let pri = prifac & 7;
                let fac = prifac & (!7);
                // Skip user messages as they've already been forwarded.
                if fac == (Facility::User as u8) {
                    return;
                }
                pri
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
            format!("<{}>{} hypervisor[0]: {}", prifac, ts, rec)
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
        let mut buffer: [u8; MAX_KMSG_RECORD] = [0; MAX_KMSG_RECORD];
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

#[allow(dead_code)]
struct KmsgPrefix<'a> {
    prifac: &'a str,
    seq: &'a str,
    timestamp_us: &'a str,
    flags: &'a str,
}

enum KmsgRecord<'a> {
    Valid(KmsgPrefix<'a>, &'a str),
    BadPrefix(&'a str, &'a str),
    NoPrefix(&'a str),
}

impl<'a> From<&'a str> for KmsgRecord<'a> {
    fn from(raw: &'a str) -> Self {
        // Data consists of "prefix;message".
        // Prefix consists of "priority+facility,seq,timestamp_us,flags".
        // Ref: https://www.kernel.org/doc/Documentation/ABI/testing/dev-kmsg
        match raw.split_once(';') {
            None => KmsgRecord::NoPrefix(raw),
            Some((prefix, msg)) => {
                let parts: Vec<&str> = prefix.split(',').collect();
                match parts.len() {
                    4 => KmsgRecord::Valid(
                        KmsgPrefix {
                            prifac: parts[0],
                            seq: parts[1],
                            timestamp_us: parts[2],
                            flags: parts[3],
                        },
                        msg,
                    ),
                    _ => KmsgRecord::BadPrefix(prefix, msg),
                }
            }
        }
    }
}

impl fmt::Display for KmsgRecord<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Produce something reasonable, even if the message cant be parsed.
        match self {
            KmsgRecord::NoPrefix(s) => write!(f, "[nopfx] {}", escape(s)),
            KmsgRecord::BadPrefix(s, msg) => write!(f, "[{}] {}", escape(s), escape(msg)),
            KmsgRecord::Valid(prefix, msg) => {
                write!(
                    f,
                    "{} {}",
                    format_kernel_ts(prefix.timestamp_us),
                    escape(msg)
                )
            }
        }
    }
}

// The kernel docs claim that unprintable chars are escaped, but that is a lie.
fn escape(line: &str) -> String {
    line.strip_suffix('\n')
        .unwrap_or(line)
        .escape_default()
        .to_string()
}

pub fn kmsg_tail(nbytes: usize) -> Result<VecDeque<String>> {
    // Estimate number of lines based on 80 chars per line.
    let mut lines: VecDeque<String> = VecDeque::with_capacity(nbytes / 80);
    let mut size: usize = 0;
    let mut buffer: [u8; MAX_KMSG_RECORD] = [0; MAX_KMSG_RECORD];
    let mut f = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NONBLOCK)
        .open(KMSG_PATH)?;
    while let Ok(n) = handle_eintr!(f.read(&mut buffer)) {
        let data = String::from_utf8_lossy(&buffer[..n]);
        let msg = format!("{}", KmsgRecord::from(data.as_ref()));
        size += msg.len();
        lines.push_back(msg);
        while size > nbytes {
            size -= lines.front().unwrap().len();
            lines.pop_front();
        }
    }
    Ok(lines)
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
