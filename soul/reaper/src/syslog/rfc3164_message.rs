// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::PartialEq;
use std::fmt::{Debug, Error, Formatter};
use std::str;

use anyhow::{bail, ensure, Context, Result};
use chrono::{DateTime, Datelike, NaiveDate, Utc};
use itertools::Itertools;

use crate::message;
use crate::syslog::message as syslog_message;
use crate::syslog::message::Pri;
use crate::syslog::{Facility, Severity, SyslogMessage};

/// Represents the reaper specific version of a message received in RFC 3164 format.
///
/// The parser is very strict and will not try to guess from the input if any extensions or
/// modifications could form a valid message, potentially with more context.
///
/// There are two exceptions where this deviates from the standard:
/// 1. Messages longer than 1024 bytes don't get truncated.
/// 2. Unprintable characters will be escaped instead of rejecting the data.
///
/// This struct should only be used as intermediate representation to turn it into a
/// `SyslogMessage` later.
///
/// # Example
/// ```
/// let data = "<42>Apr  1 12:34:56 localhost DocExample This is an example for a RFC3164 message";
/// let syslog_message: Box<dyn SyslogMessage> = data.parse::<Rfc3164Message>().unwrap().into();
///
/// process(syslog_message);
/// ```
pub struct Rfc3164Message {
    application_name: Box<str>,
    facility: Facility,
    message: Box<[u8]>,
    message_offset: usize,
    severity: Severity,
    /// The RFC requires the timestamp to be in local time but since we will use only UTC from here
    /// on it doesn't make a difference if we store it here in UTC already.
    timestamp: DateTime<Utc>,
}

#[cfg(not(test))]
fn current_time() -> DateTime<Utc> {
    chrono::offset::Utc::now()
}

#[cfg(test)]
thread_local! {
static CURRENT_TIME: std::cell::Cell<DateTime<Utc>> =
    std::cell::Cell::new(chrono::DateTime::UNIX_EPOCH);
}

#[cfg(test)]
fn current_time() -> DateTime<Utc> {
    CURRENT_TIME.get()
}

/// For RFC 3164 the timestamp is always the same length and format.
const TIMESTAMP_LEN: usize = "Apr  1 12:34:56".len();

impl Default for Rfc3164Message {
    fn default() -> Self {
        // 13 is defined by the RFC in 4.3.3 as the value to use if the PRI can't be
        //    determined.
        let pri = Pri::try_from(13).unwrap();
        Rfc3164Message {
            application_name: Box::from(""),
            facility: pri.facility(),
            message: [].into(),
            message_offset: 0,
            severity: pri.severity(),
            timestamp: current_time(),
        }
    }
}

impl Debug for Rfc3164Message {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        f.debug_struct("Rfc3164Message")
            .field("application_name", &self.application_name)
            .field("facility", &self.facility)
            .field("message", &message::redact(&self.message))
            .field("message_offset", &self.message_offset)
            .field("severity", &self.severity)
            .field("timestamp", &self.timestamp)
            .finish()
    }
}

impl PartialEq for Rfc3164Message {
    fn eq(&self, other: &Rfc3164Message) -> bool {
        self.application_name() == other.application_name()
            && self.facility() == other.facility()
            && self.message() == other.message()
            && self.severity() == other.severity()
            && self.timestamp() == other.timestamp()
    }
}

impl SyslogMessage for Rfc3164Message {
    fn application_name(&self) -> &str {
        &self.application_name
    }

    fn facility(&self) -> Facility {
        self.facility
    }

    fn message(&self) -> &[u8] {
        &self.message[self.message_offset..]
    }

    fn severity(&self) -> Severity {
        self.severity
    }
    fn timestamp(&self) -> DateTime<Utc> {
        self.timestamp
    }
}

impl Rfc3164Message {
    /// Parse any string input into a RFC 3164 message.
    ///
    /// There is no such thing as an invalid RFC 3164 message (unlike RFC 5424).
    /// Any data can be valid and should be treated as such. The receiver has to make an effort to
    /// decipher as much as possible within the RFC guidelines and once it encounters something
    /// unexpected treat everything after the last known good bit as `content`.
    ///
    /// This function will start with the pessimistic approach of a text-only string, i.e. the
    /// whole `data` as `content` field and then start choping away at the beginning as it
    /// encounters valid data.
    ///
    /// Any characters which aren't printable, i.e. control characters, will be escaped. For this
    /// instance the collector acts as a relay fixing the otherwise invalid message it received.
    fn parse(&mut self, data: &[u8]) -> Result<()> {
        ensure!(!data.is_empty(), "Empty messages are invalid");

        self.message = Box::from(data);

        let text = match str::from_utf8(data) {
            Ok(s) => s,
            Err(err) => {
                let valid = err.valid_up_to();
                log::warn!(
                    "Failed to parse data after {valid} bytes as UTF-8. Skipping message \
                    validation for the remaining {} bytes",
                    data.len() - valid
                );
                str::from_utf8(&data[..valid]).expect("UTF-8 error indicated valid position")
            }
        };

        let (pri, remaining) = syslog_message::parse_pri(text).context("Didn't find valid PRI")?;

        self.facility = pri.facility();
        self.severity = pri.severity();

        self.message_offset += text.len() - remaining.len();

        let ts_str = remaining
            .get(..TIMESTAMP_LEN)
            .context("String too short for timestamp")?;
        self.timestamp = parse_timestamp(ts_str).context("Couldn't parse timestamp")?;

        self.message_offset += TIMESTAMP_LEN;

        let after_ts = remaining
            .get(TIMESTAMP_LEN..)
            .context("No more data after timestamp")?;

        ensure!(!after_ts.is_empty(), "No more data after timestamp");

        ensure!(
            after_ts.starts_with(' '),
            "Timestamp must be followed by a space"
        );
        self.message_offset += 1;

        let (hostname, msg) = &after_ts[1..]
            .split_once(' ')
            .context("Timestamp must be followed by hostname surrounded by spaces")?;

        ensure!(
            is_print_us_ascii(hostname),
            "Hostname must be valid ASCII %d32-126"
        );

        self.message_offset += hostname.len();

        let tag_offset = match msg
            .as_bytes()
            .iter()
            .take(33)
            .position(|&b| !b.is_ascii_alphanumeric())
        {
            Some(idx) => idx,
            None => bail!("No end of tag found in first 33 characters"),
        };

        self.application_name = msg
            .get(..tag_offset)
            .context("Couldn't get tag from offset")?
            .into();
        self.message_offset += tag_offset + 1;

        if data.len() == text.len() {
            let mut message = String::with_capacity(data.len() - self.message_offset);
            for c in text
                .get(self.message_offset..)
                .context("Couldn't get valid range from current message offset")?
                .chars()
            {
                if c.is_control() {
                    message.extend(c.escape_default());
                } else {
                    message.push(c);
                }
            }
            self.message_offset = 0;
            self.message = message.into_bytes().into();
        }

        Ok(())
    }
}

fn is_print_us_ascii(data: &str) -> bool {
    data.as_bytes().iter().all(|b| (32..=126).contains(b))
}

/// Attempts to parse a string as a timestamp.
///
/// This can fail, e.g. when wrong delimiters are used or when data is missing.
/// The checks are very strict and don't try to fix up data that doesn't adhere
/// to the RFC but could be interpreted by a human as a valid timestamp.
fn parse_timestamp(text: &str) -> Result<DateTime<Utc>> {
    let now = current_time();

    ensure!(
        is_print_us_ascii(text),
        "Timestamp must only contain ASCII characters %d32-126"
    );

    ensure!(
        text.len() == TIMESTAMP_LEN,
        "RFC 3164 timestamp must be exactly {TIMESTAMP_LEN} characters but is {} characters long",
        text.len()
    );

    let (month_part, remaining) = text.split_at(3);

    let month = match month_part {
        "Jan" => 1,
        "Feb" => 2,
        "Mar" => 3,
        "Apr" => 4,
        "May" => 5,
        "Jun" => 6,
        "Jul" => 7,
        "Aug" => 8,
        "Sep" => 9,
        "Oct" => 10,
        "Nov" => 11,
        "Dec" => 12,
        _ => bail!("Unrecognized month"),
    };

    let (day_part, time_part) = remaining.split_at(3);

    ensure!(
        day_part.starts_with(' '),
        "First character after month must be a space"
    );
    ensure!(
        day_part.as_bytes()[1] != b'0',
        "Days must not have leading zeros"
    );

    let day = day_part.trim_start().parse()?;

    let date = NaiveDate::from_ymd_opt(now.year(), month, day).with_context(|| {
        format!(
            "Couldn't create valid day from {}-{month}-{day}",
            now.year()
        )
    })?;

    let (hour, minute, second) = time_part
        .splitn(3, ':')
        .collect_tuple()
        .context("Couldn't find 3 elements separated by : for time parsing")?;

    ensure!(
        hour.starts_with(' '),
        "First character after day of month must be a space"
    );

    let date_time = date
        .and_hms_opt(hour.trim_start().parse()?, minute.parse()?, second.parse()?)
        .with_context(|| format!("Couldn't create valid time from {hour}:{minute}:{second}"))?;

    let utc_date = date_time.and_utc();

    // The timestamp doesn't include a year so we have to add the current year to
    // make it a valid timestamp. This can lead to problems around new-year as a
    // message may be created Dec 31st 23:59:59 but received and processed on
    // Jan 1st 00:00:00. This puts the message into the future and may make
    // debugging unnecessarily hard when sorting by datetime. So we do a check
    // here if we should move the timestamp 1 year back.
    if utc_date.day() == 31
        && utc_date.month() == 12
        && utc_date > now
        && now.day() == 1
        && now.month() == 1
    {
        return Ok(utc_date.with_year(now.year() - 1).unwrap_or(utc_date));
    }

    Ok(utc_date)
}

impl From<&[u8]> for Rfc3164Message {
    fn from(data: &[u8]) -> Self {
        if data.len() > 1024 {
            log::warn!("Received message longer than 1024 bytes. This is a violation of the RFC");
        }
        let mut message = Rfc3164Message::default();
        if let Err(err) = message.parse(data) {
            log::warn!("Received message but failed to fully parse: {err:#}");
        }
        message
    }
}

#[cfg(test)]
#[path = "rfc3164_message_test.rs"]
mod tests;
