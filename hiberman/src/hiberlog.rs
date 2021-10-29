// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement consistent logging across the hibernate and resume transition.
use std::fs::{File, OpenOptions};
use std::io::{BufRead, BufReader, Cursor, Read, Write};
use std::str;
use std::sync::MutexGuard;
use std::time::Instant;

use anyhow::{Context, Result};
use log::{debug, warn, Level, LevelFilter, Log, Metadata, Record};
use once_cell::sync::OnceCell;
use sync::Mutex;
use syslog::{BasicLogger, Facility, Formatter3164};

use crate::diskfile::BouncedDiskFile;
use crate::files::open_log_file;

/// Define the path to kmsg, used to send log lines into the kernel buffer in
/// case a crash occurs.
const KMSG_PATH: &str = "/dev/kmsg";
/// Define the prefix to go on log messages.
const LOG_PREFIX: &str = "hiberman";
/// Define the default flush threshold. This must be a power of two.
const FLUSH_THRESHOLD: usize = 4096;

static STATE: OnceCell<Mutex<Hiberlog>> = OnceCell::new();

fn get_state() -> Result<&'static Mutex<Hiberlog>> {
    STATE.get_or_try_init(|| Hiberlog::new().map(Mutex::new))
}

fn lock() -> Result<MutexGuard<'static, Hiberlog>> {
    get_state().map(Mutex::lock)
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
        };
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
        let mut state = lock!();
        state.flush_full_pages()
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
    partial: Option<Vec<u8>>,
    pending: Vec<Vec<u8>>,
    pending_size: usize,
    flush_threshold: usize,
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
            partial: None,
            pending: vec![],
            pending_size: 0,
            flush_threshold: FLUSH_THRESHOLD,
            to_kmsg: false,
            out: HiberlogOut::Syslog,
            pid: std::process::id(),
            syslogger,
        })
    }

    /// Log a record.
    fn log_record(&mut self, record: &Record) {
        let mut buf = [0u8; 1024];

        // If sending to the syslog, just forward there and exit.
        if matches!(self.out, HiberlogOut::Syslog) {
            self.syslogger.log(record);
            return;
        }

        let res = {
            let mut buf_cursor = Cursor::new(&mut buf[..]);
            let facprio = priority_from_level(record.level()) + (Facility::LOG_USER as usize);
            if let Some(file) = record.file() {
                let duration = self.start.elapsed();
                write!(
                    &mut buf_cursor,
                    "<{}>{}: {}.{:03} {} [{}:{}] ",
                    facprio,
                    LOG_PREFIX,
                    duration.as_secs(),
                    duration.subsec_millis(),
                    self.pid,
                    file,
                    record.line().unwrap_or(0)
                )
            } else {
                write!(&mut buf_cursor, "<{}>{}: ", facprio, LOG_PREFIX)
            }
            .and_then(|()| writeln!(&mut buf_cursor, "{}", record.args()))
            .map(|()| buf_cursor.position() as usize)
        };

        if let Ok(len) = &res {
            if self.to_kmsg {
                let _ = self.kmsg.write_all(&buf[..*len]);
            }

            self.pending.push(buf[..*len].to_vec());
            self.pending_size += *len;
            self.flush_full_pages();
        }
    }

    /// Helper function to flush one page's worth of buffered log lines to a
    /// file destination.
    fn flush_one_page(&mut self) {
        // Do nothing if buffering messages in memory.
        if matches!(self.out, HiberlogOut::BufferInMemory) {
            return;
        }

        // Start with the partial string from last time, or an empty buffer.
        let mut buf = Vec::<u8>::new();
        if let Some(v) = &self.partial {
            buf.extend(v);
        }

        self.partial = None;
        let mut partial = None;

        // Add as many whole lines into the buffer as will fit.
        let mut length = buf.len();
        let mut i = 0;
        while (i < self.pending.len()) && (length + self.pending[i].len() <= self.flush_threshold) {
            buf.extend(&self.pending[i]);
            length += self.pending[i].len();
            i += 1;
        }

        // Add a partial line or pad out the space if needed.
        if length < self.flush_threshold {
            let remainder = self.flush_threshold - length;
            if i < self.pending.len() {
                // Add a part of this line to the buffer to fill it out.
                buf.extend(&self.pending[i][..remainder]);
                length += remainder;

                // Save the rest of this line as the next partial, and advance over it.
                partial = Some(self.pending[i][remainder..].to_vec());
                i += 1;
            } else {
                // Fill the buffer with zeroes as a signal to stop reading.
                buf.extend(vec![0x0u8; remainder]);
            }
        }

        if let HiberlogOut::File(f) = &mut self.out {
            let _ = f.write_all(&buf[..]);
        }

        self.pending_size -= length;
        self.pending = self.pending[i..].to_vec();
        self.partial = partial;
    }

    /// Flush all complete pages of log lines.
    pub fn flush_full_pages(&mut self) {
        // Do nothing if buffering messages in memory.
        if matches!(self.out, HiberlogOut::BufferInMemory) {
            return;
        }

        while self.pending_size >= self.flush_threshold {
            self.flush_one_page();
        }
    }

    /// Flush and finalize the log file. This is used to terminate the logs
    /// written to a file, to make sure that they are all written out, and that
    /// when retrieved later the end of the log is known.
    pub fn flush(&mut self) {
        // Do a regular full-page flush, which will be perfectly page aligned.
        self.flush_full_pages();
        // Flush one more page, which serves two purposes:
        // 1. Flushes out a partial page.
        // 2. Ensures that there's padding at the end, even if the data
        //    perfectly lines up with a page. This is used on read to know
        //    when to stop.
        self.flush_one_page();
    }

    /// Push any pending lines to the syslog.
    pub fn flush_to_syslog(&mut self) {
        // Ignore the partial line, just replay pending lines.
        for line_vec in &self.pending {
            let mut len = line_vec.len();
            if len == 0 {
                continue;
            }

            len -= 1;
            let s = match str::from_utf8(&line_vec[0..len]) {
                Ok(v) => v,
                Err(_) => continue,
            };
            replay_line(&self.syslogger, s.to_string());
        }

        self.reset();
    }

    /// Empty the pending log buffer, discarding any unwritten messages. This is
    /// used after a successful resume to avoid replaying what look like
    /// unflushed logs from when the snapshot was taken. In reality these logs
    /// got flushed after the snapshot was taken, just before the machine shut
    /// down.
    pub fn reset(&mut self) {
        self.pending_size = 0;
        self.pending = vec![];
        self.partial = None;
    }
}

/// Divert the log to a new output. This does not flush or reset the stream, the
/// caller must decide what they want to do with buffered output before calling
/// this.
pub fn redirect_log(out: HiberlogOut) {
    log::logger().flush();
    let mut state = lock!();
    state.to_kmsg = false;
    match out {
        HiberlogOut::BufferInMemory => {}
        // If going back to syslog, dump any pending state into syslog.
        HiberlogOut::Syslog => state.flush_to_syslog(),
        HiberlogOut::File(_) => {
            // Any time we're redirecting to a file, also send to kmsg as a
            // message in a bottle, in case we never get a chance to replay our
            // own file logs. This shouldn't produce duplicate messages on
            // success because when we're logging to a file we're also
            // barrelling towards a kexec or shutdown.
            state.to_kmsg = true;
        }
    }

    state.out = out;
}

/// Discard any buffered but unsent logging data.
pub fn reset_log() {
    let mut state = lock!();
    state.reset();
}

/// Flush any pending messages out to the file, and add a terminator.
pub fn flush_log() {
    let mut state = lock!();
    state.flush();
}

/// Write a newline to the beginning of the given log file so that future
/// attempts to replay that log will see it as empty. This doesn't securely
/// shred the log data.
pub fn clear_log_file(file: &mut BouncedDiskFile) -> Result<()> {
    let mut buf = [0u8; FLUSH_THRESHOLD];
    buf[0] = b'\n';
    file.rewind()?;
    file.write_all(&buf).context("Failed to clear log file")?;
    Ok(())
}

/// Define the known log file types.
pub enum HiberlogFile {
    Suspend,
    Resume,
}

/// Replay the suspend (and maybe resume) logs to the syslogger.
pub fn replay_logs(push_resume_logs: bool, clear: bool) {
    // Push the hibernate logs that were taken after the snapshot (and
    // therefore after syslog became frozen) back into the syslog now.
    // These should be there on both success and failure cases.
    replay_log(HiberlogFile::Suspend, clear);

    // If successfully resumed from hibernate, or in the bootstrapping kernel
    // after a failed resume attempt, also gather the resume logs
    // saved by the bootstrapping kernel.
    if push_resume_logs {
        replay_log(HiberlogFile::Resume, clear);
    }
}

/// Helper function to replay the suspend or resume log to the syslogger, and
/// potentially zero out the log as well.
fn replay_log(log_file: HiberlogFile, clear: bool) {
    let name = match log_file {
        HiberlogFile::Suspend => "suspend log",
        HiberlogFile::Resume => "resume log",
    };

    let mut opened_log = match open_log_file(log_file) {
        Ok(f) => f,
        Err(e) => {
            warn!("Failed to open {}: {}", name, e);
            return;
        }
    };

    replay_log_file(&mut opened_log, name);
    if clear {
        if let Err(e) = clear_log_file(&mut opened_log) {
            warn!("Failed to clear {}: {}", name, e);
        }
    }
}

/// Replay a generic log file to the syslogger..
fn replay_log_file(file: &mut dyn Read, name: &str) {
    // Read the file until the first null byte is found, which signifies the end
    // of the log.
    let mut reader = BufReader::new(file);
    let mut buf = Vec::<u8>::new();
    if let Err(e) = reader.read_until(0, &mut buf) {
        warn!("Failed to replay log file: {}", e);
        return;
    }

    let syslogger = create_syslogger();
    syslogger.log(
        &Record::builder()
            .args(format_args!("Replaying {}:", name))
            .level(Level::Info)
            .build(),
    );
    // Now split that big buffer into lines and feed it into the log.
    let len_without_delim = buf.len() - 1;
    let cursor = Cursor::new(&buf[..len_without_delim]);
    for line in cursor.lines() {
        let line = match line {
            Ok(l) => l,
            Err(_) => continue,
        };

        replay_line(&syslogger, line);
    }

    syslogger.log(
        &Record::builder()
            .args(format_args!("Done replaying {}", name))
            .level(Level::Info)
            .build(),
    );
}

/// Replay a single log line to the syslogger.
fn replay_line(syslogger: &BasicLogger, line: String) {
    // The log lines are in kmsg format, like:
    // <11>hiberman: [src/hiberman.rs:529] Hello 2004
    // Trim off the first colon, everything after is line contents.
    let mut elements = line.splitn(2, ": ");
    let header = elements.next().unwrap();
    let contents = match elements.next() {
        Some(c) => c,
        None => {
            warn!(
                "Failed to split on colon: header: {}, line {:x?}, len {}",
                header,
                line.as_bytes(),
                line.len()
            );
            return;
        }
    };

    // Now trim <11>hiberman into <11, and parse 11 out of the combined
    // priority + facility.
    let facprio_string = header.splitn(2, '>').next().unwrap();
    let facprio: u8 = match facprio_string[1..].parse() {
        Ok(i) => i,
        Err(_) => {
            warn!("Failed to parse facprio for next line, using debug");
            debug!("{}", contents);
            return;
        }
    };

    let level = level_from_u8(facprio & 7);
    syslogger.log(
        &Record::builder()
            .args(format_args!("{}", contents))
            .level(level)
            .build(),
    );
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
        pid: std::process::id() as i32,
    };

    let logger = syslog::unix(formatter).expect("Could not connect to syslog");
    BasicLogger::new(logger)
}
