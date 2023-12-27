// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement consistent logging across the hibernate and resume transition.
use std::fs::File;
use std::fs::OpenOptions;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Write;
use std::str;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::time::Duration;
use std::time::Instant;

use anyhow::anyhow;
use anyhow::Context;
use anyhow::Result;
use log::warn;
use log::Level;
use log::LevelFilter;
use log::Log;
use log::Metadata;
use log::Record;
use once_cell::sync::OnceCell;
use syslog::BasicLogger;
use syslog::Facility;
use syslog::Formatter3164;

use crate::hiberutil::HibernateStage;
use crate::journal::LogFile;
use crate::volume::ActiveMount;

/// Define the path to kmsg, used to send log lines into the kernel buffer in
/// case a crash occurs.
const KMSG_PATH: &str = "/dev/kmsg";
/// Define the prefix to go on log messages.
const LOG_PREFIX: &str = "hiberman";

static STATE: OnceCell<Mutex<Hiberlog>> = OnceCell::new();

fn get_state() -> Result<&'static Mutex<Hiberlog>> {
    STATE.get_or_try_init(|| Hiberlog::new().map(Mutex::new))
}

fn lock() -> Result<MutexGuard<'static, Hiberlog>> {
    get_state().map(|m| m.lock().unwrap())
}

/// Initialize the syslog connection and internal variables.
pub fn init() -> Result<()> {
    // Warm up to initialize the state.
    let _ = get_state()?;
    log::set_boxed_logger(Box::new(HiberLogger::new()))
        .map(|()| log::set_max_level(LevelFilter::Debug))?;
    Ok(())
}

// Attempts to lock and retrieve the state. Returns from the function silently on failure.
macro_rules! lock {
    () => {
        match lock() {
            Ok(s) => s,
            _ => return,
        }
    };
}

/// Define the instance that gets handed to the logging crate.
struct HiberLogger {}

impl HiberLogger {
    pub fn new() -> Self {
        HiberLogger {}
    }
}

impl Log for HiberLogger {
    fn enabled(&self, _metadata: &Metadata) -> bool {
        true
    }

    fn log(&self, record: &Record) {
        let mut state = lock!();
        state.log_record(record)
    }

    fn flush(&self) {
        // nothing to do with O_SYNC files.
    }
}

/// Define the possibilities as to where to route log lines to.
pub enum HiberlogOut {
    /// Don't push log lines anywhere for now, just keep them in memory.
    BufferInMemory,
    /// Push log lines to the syslogger.
    Syslog,
    /// Push log lines to a File-like object.
    File(Box<dyn Write + Send>),
}

/// Define the (singleton) hibernate logger state.
struct Hiberlog {
    kmsg: File,
    start: Instant,
    pending: Vec<Vec<u8>>,
    to_kmsg: bool,
    out: HiberlogOut,
    pid: u32,
    syslogger: BasicLogger,
}

impl Hiberlog {
    pub fn new() -> Result<Self> {
        let kmsg = OpenOptions::new()
            .read(true)
            .write(true)
            .open(KMSG_PATH)
            .context("Failed to open kernel message logger")?;

        let syslogger = create_syslogger();
        Ok(Hiberlog {
            kmsg,
            start: Instant::now(),
            pending: vec![],
            to_kmsg: false,
            out: HiberlogOut::Syslog,
            pid: std::process::id(),
            syslogger,
        })
    }

    /// Log a record.
    fn log_record(&mut self, record: &Record) {
        let duration = self.start.elapsed();
        let l = record_to_rfc3164_line(record, self.pid, duration);

        match &mut self.out {
            HiberlogOut::BufferInMemory => {
                if self.to_kmsg {
                    let _ = self.kmsg.write_all(&l);
                }

                self.pending.push(l)
            }
            HiberlogOut::File(f) => {
                if self.to_kmsg {
                    let _ = self.kmsg.write_all(&l);
                }

                let _ = f.write_all(&l);
            }
            // If sending to the syslog, just forward there
            HiberlogOut::Syslog => self.syslogger.log(record)
        }
    }

    fn flush(&mut self) {
        match &mut self.out {
            // Write any ending lines to the file.
            HiberlogOut::File(f) => {
                // self.pending will be empty if previously not logging to memory.
                if self.pending.is_empty() { return; }
                map_log_entries(&self.pending, |s| {
                    let _ = f.write_all(&[s, &[b'\n']].concat());
                });
                self.reset();
            }
            // Push any pending lines to the syslog.
            HiberlogOut::Syslog => {
                map_log_entries(&self.pending, |s| {
                    replay_line(&self.syslogger, "M", s);
                });
                self.reset();
            }
            // In memory buffering flushes are a nop.
            HiberlogOut::BufferInMemory => {}
        }
    }

    /// Empty the pending log buffer, discarding any unwritten messages. This is
    /// used after a successful resume to avoid replaying what look like
    /// unflushed logs from when the snapshot was taken. In reality these logs
    /// got flushed after the snapshot was taken, just before the machine shut
    /// down.
    pub fn reset(&mut self) {
        self.pending = vec![];
    }
}

fn map_log_entries<F>(entries: &[Vec<u8>], mut f: F)
where
    F: FnMut(&[u8]),
{
    entries.iter().filter(|e| !e.is_empty()).for_each(|e| f(e.as_slice()));
}

/// Divert the log to a new output. If the log was previously pointing to syslog
/// or a file, those messages are flushed. If the log was previously being
/// stored in memory, those messages will naturally flush to the given new
/// destination.
pub fn redirect_log(out: HiberlogOut) {
    log::logger().flush();
    let mut state = lock!();
    state.to_kmsg = false;

    // Any time we're redirecting to a file, also send to kmsg as a
    // message in a bottle, in case we never get a chance to replay our
    // own file logs. This shouldn't produce duplicate messages on
    // success because when we're logging to a file we're also
    // barrelling towards a kexec or shutdown.
    state.to_kmsg = matches!(out, HiberlogOut::File(_));
    state.out = out;

    state.flush();
}

/// Discard any buffered but unsent logging data.
pub fn reset_log() {
    let mut state = lock!();
    state.reset();
}

/// Replay the suspend (and maybe resume) logs to the syslogger.
pub fn replay_logs(_: &ActiveMount, push_resume_logs: bool, clear: bool) {
    // Push the hibernate logs that were taken after the snapshot (and
    // therefore after syslog became frozen) back into the syslog now.
    // These should be there on both success and failure cases.
    replay_log_file(HibernateStage::Suspend, clear);

    // If successfully resumed from hibernate, or in the bootstrapping kernel
    // after a failed resume attempt, also gather the resume logs
    // saved by the bootstrapping kernel.
    if push_resume_logs {
        replay_log_file(HibernateStage::Resume, clear);
    }
}

/// Helper function to replay the suspend or resume log file
/// to the syslogger, and potentially zero out the log as well.
fn replay_log_file(stage: HibernateStage, clear: bool) {
    if !LogFile::exists(stage) {
        return;
    }

    let (name, prefix) = match stage {
        HibernateStage::Suspend => ("suspend log", "S"),
        HibernateStage::Resume => ("resume log", "R"),
    };

    let syslogger = create_syslogger();
    syslogger.log(
        &Record::builder()
            .args(format_args!("Replaying {}:", name))
            .level(Level::Info)
            .build(),
    );

    let f = match LogFile::open(stage) {
        Ok(f) => f,
        Err(e) => {
            warn!("{}", e);
            return;
        }
    };

    let reader = BufReader::new(f);
    for line in reader.lines() {
        if let Ok(line) = line {
            replay_line(&syslogger, prefix, line.as_bytes());
        } else {
            warn!("Invalid line in log file!");
        }
    }

    syslogger.log(
        &Record::builder()
            .args(format_args!("Done replaying {}", name))
            .level(Level::Info)
            .build(),
    );

    if clear {
        LogFile::clear(stage);
    }
}

/// Replay a single log line to the syslogger.
fn replay_line(syslogger: &BasicLogger, prefix: &str, s: &[u8]) {
    // The log lines are in kmsg format, like:
    // <11>hiberman: R [src/hiberman.rs:529] Hello 2004
    // Trim off the first colon, everything after is line contents.

    // Non-UTF8 log lines shall be considered as empty strings.
    let line = str::from_utf8(s).unwrap_or_default();
    if line.is_empty() {
        return;
    }

    match parse_rfc3164_record(line) {
        Ok((contents, level)) => {
            syslogger.log(
                &Record::builder()
                    .args(format_args!("{} {}", prefix, contents))
                    .level(level)
                    .build(),
            );
        },
        Err(e) => {
            warn!("{}", e);
        }
    }
}

fn parse_rfc3164_record(line: &str) -> Result<(&str, Level)> {
    let mut elements = line.splitn(2, ": ");
    let header = elements.next().unwrap();
    let contents = elements.next().ok_or_else(|| {
            anyhow!(
                "Failed to split on colon: header: {}, line {:x?}, len {}",
                header,
                line.as_bytes(),
                line.len()
            )
    })?;

    // Now trim <11>hiberman into <11, and parse 11 out of the combined
    // priority + facility.
    let facprio_string = header.split_once('>').map_or(header, |x| x.0);
    if facprio_string.len() < 2 {
        return Err(anyhow!("Failed to extract facprio string for next line, '{}'", contents));
    }

    let level = match facprio_string[1..].parse::<u8>() {
        Ok(v) => level_from_u8(v & 7),
        Err(_) => {
            return Err(anyhow!("Failed to parse facprio for next line, '{}'", contents));
        }
    };

    Ok((contents, level))
}

fn record_to_rfc3164_line(record: &Record, pid: u32, duration: Duration) -> Vec<u8> {
    let mut buf = Vec::new();
    let facprio = priority_from_level(record.level()) + (Facility::LOG_USER as usize);
    if let Some(file) = record.file() {
        let _ = writeln!(
            &mut buf,
            "<{}>{}: {}.{:03} {} [{}:{}] {}",
            facprio,
            LOG_PREFIX,
            duration.as_secs(),
            duration.subsec_millis(),
            pid,
            file,
            record.line().unwrap_or(0),
            record.args()
        );
    } else {
        let _ = writeln!(&mut buf, "<{}>{}: {}", facprio, LOG_PREFIX, record.args());
    }

    buf
}

fn level_from_u8(value: u8) -> Level {
    match value {
        0 => Level::Error,
        1 => Level::Error,
        2 => Level::Error,
        3 => Level::Error,
        4 => Level::Warn,
        5 => Level::Info,
        6 => Level::Info,
        7 => Level::Debug,
        _ => Level::Debug,
    }
}

fn priority_from_level(level: Level) -> usize {
    match level {
        Level::Error => 3,
        Level::Warn => 4,
        Level::Info => 6,
        Level::Debug => 7,
        Level::Trace => 7,
    }
}

fn create_syslogger() -> BasicLogger {
    let formatter = Formatter3164 {
        facility: Facility::LOG_USER,
        hostname: None,
        process: "hiberman".into(),
        pid: std::process::id(),
    };

    let logger = syslog::unix(formatter).expect("Could not connect to syslog");
    BasicLogger::new(logger)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_rfc3164_record_good() {
        let l = "<11>hiberman: R [src/hiberman.rs:529] Hello 2004";
        let rec = parse_rfc3164_record(l).unwrap();
        assert_eq!(rec, ("R [src/hiberman.rs:529] Hello 2004", Level::Error));
    }

    #[test]
    #[should_panic(expected = "Failed to split on colon: header: ")]
    fn test_parse_rfc3164_record_bad_colon() {
        let l = "<11>hiberman R [src/hiberman.rs:529] Hello 2004";
        parse_rfc3164_record(l).unwrap();
    }

    #[test]
    #[should_panic(expected = "Failed to parse facprio for next line, ")]
    fn test_parse_rfc3164_record_bad_facprio() {
        let l = "<XX>hiberman: R [src/hiberman.rs:529] Hello 2004";
        parse_rfc3164_record(l).unwrap();
    }

    #[test]
    fn test_record_to_rfc3164_line_no_file() {
        let r = Record::builder()
            .args(format_args!("XXX"))
            .level(Level::Info)
            .build();
        let m = record_to_rfc3164_line(&r, 0, Duration::new(0, 0));
        assert_eq!(str::from_utf8(&m).unwrap(), "<14>hiberman: XXX\n");
    }

    #[test]
    fn test_record_to_rfc3164_line_file() {
        let r = Record::builder()
            .args(format_args!("XXX"))
            .level(Level::Info)
            .file(Some("/the/file"))
            .build();
        let m = record_to_rfc3164_line(&r, 0, Duration::new(0, 0));
        assert_eq!(str::from_utf8(&m).unwrap(), "<14>hiberman: 0.000 0 [/the/file:0] XXX\n");
    }
}
