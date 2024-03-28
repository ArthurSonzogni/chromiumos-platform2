// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{Debug, Error, Formatter};

use anyhow::{bail, ensure, Context, Result};
use chrono::{DateTime, Utc};

use crate::message;
use crate::syslog::message as syslog_message;
use crate::syslog::{Facility, Severity, SyslogMessage};

/// The field separator (SP in the RFC) is 0x20. When printing other UTF-8
/// spacelike characters this can be confusing why a field is/isn't recognized.
const FIELD_SEPARATOR: char = 0x20 as char;
const NILVALUE: &str = "-";

/// Represents the reaper specific version of a message received in RFC 5424 format.
///
/// This struct should only be used as intermediate representation to turn it into a
/// `SyslogMessage` later.
///
/// # Example
/// ```
/// let data = "<165>1 2003-08-24T05:14:15.000003-07:00 192.0.2.1 myproc \
///     8710 - - %% It's time to make the do-nuts.";
/// let syslog_message: Box<dyn SyslogMessage> = data.parse::<Rfc5424Message>().unwrap().into();
///
/// process(syslog_message);
/// ```
#[derive(PartialEq)]
pub struct Rfc5424Message {
    application_name: Box<str>,
    facility: Facility,
    message: Box<str>,
    severity: Severity,
    timestamp: DateTime<Utc>,
}

impl Debug for Rfc5424Message {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        f.debug_struct("Rfc5424Message")
            .field("application_name", &self.application_name)
            .field("facility", &self.facility)
            .field("message", &message::redact(&self.message()))
            .field("severity", &self.severity)
            .field("timestamp", &self.timestamp)
            .finish()
    }
}

impl SyslogMessage for Rfc5424Message {
    fn application_name(&self) -> &str {
        &self.application_name
    }

    fn facility(&self) -> Facility {
        self.facility
    }

    fn message(&self) -> &str {
        &self.message
    }

    fn severity(&self) -> Severity {
        self.severity
    }
    fn timestamp(&self) -> DateTime<Utc> {
        self.timestamp
    }
}

struct MaxLen(usize);

/// Printable US ASCII characters are defined by RFC 5424 as [33,126].
///
/// Unlike RFC 3164 0x20 is not part of the 'printable' character set as it's used as a delimiter
/// here instead.
fn is_rfc5424_print_us_ascii(data: &str) -> bool {
    data.as_bytes().iter().all(|b| (33..=126).contains(b))
}

fn decode_pri(pri_version: &str) -> Result<(Facility, Severity, u8)> {
    ensure_valid_field(pri_version, "PRI and version", MaxLen(7))?;

    let (pri, version) = syslog_message::parse_pri(pri_version)?;
    Ok((pri.facility(), pri.severity(), version.parse::<u8>()?))
}

fn parse_timestamp(time_string: &str) -> Result<DateTime<Utc>> {
    const MAX_LEN: MaxLen = MaxLen("YYYY-MM-DDTHH:MM:SS.ffffff+HH:MM".len());
    ensure_valid_field(time_string, "timestamp", MAX_LEN)?;

    if time_string == NILVALUE {
        log::debug!("Got nil-value for timestamp. Using NOW as timestamp");
        return Ok(chrono::offset::Utc::now());
    }

    const MIN_LEN: usize = "YYYY-MM-DDTHH:MM:SSZ".len();
    ensure!(
        time_string.len() >= MIN_LEN,
        "Timestamp is too short to be valid RFC 3339"
    );
    ensure!(
        time_string.contains('T'),
        "RFC 5424 must contain upper case T between date and time"
    );
    ensure!(
        time_string.get(17..=18) != Some("60"),
        "Leap seconds must not be used in RFC 5424 timestamps"
    );
    ensure!(
        !time_string.ends_with('z'),
        "RFC 5424 timestamps must end in upper case Z or numerical offset"
    );
    if let Some((_, fractional_tz)) = time_string.split_once('.') {
        let fractional = fractional_tz
            .chars()
            .take_while(|&c| c.is_ascii_digit())
            .collect::<String>();
        ensure!(
            fractional.len() <= 6,
            "timestamp must not contain more than 6 fractional digits of a second"
        );
    }

    let rfc3339_ts = DateTime::parse_from_rfc3339(time_string)?;

    Ok(rfc3339_ts.into())
}

fn parse_application_name(app_name: &str) -> Result<&str> {
    ensure_valid_field(app_name, "application name", MaxLen(48))?;
    if app_name == NILVALUE {
        return Ok("");
    }
    Ok(app_name)
}

fn ensure_valid_field(field: &str, field_name: &str, max_length: MaxLen) -> Result<()> {
    ensure!(!field.is_empty(), "{field_name} must not be empty");
    ensure!(
        field.len() <= max_length.0,
        "{field_name} must be at most {} ASCII characters",
        max_length.0
    );
    ensure!(
        is_rfc5424_print_us_ascii(field),
        "{field_name} must be valid ASCII [33,126]"
    );

    Ok(())
}

fn strip_bom(text: &str) -> &str {
    const UTF8_BOM: &str = "\u{FEFF}";
    text.strip_prefix(&UTF8_BOM).unwrap_or(text)
}

/// Extract the structured data part and message from the input text.
///
/// This doesn't validate the structured data part, this has to be done by
/// another function later.
fn split_structured_data_and_message(text: &str) -> Result<(&str, &str)> {
    ensure!(
        !text.is_empty(),
        "Valid RFC 5424 messages must have data after the header fields"
    );
    if text == NILVALUE {
        return Ok(("", ""));
    }

    const OPENING_STRUCTURED_DATA_CHAR: char = '[';
    match text.get(..1) {
        Some(c) => match c {
            NILVALUE => {
                let message = match text.split_once(FIELD_SEPARATOR) {
                    Some((_, msg)) => msg,
                    None => "",
                };
                ensure!(
                    text.len() - message.len() == "- ".len(),
                    "Hyphen for structured data must be followed by space or \
                    nothing to be valid RFC 5424 message"
                );
                return Ok(("", message));
            }
            c => ensure!(
                c.starts_with(OPENING_STRUCTURED_DATA_CHAR),
                "First character after header fields must either be {NILVALUE} or ["
            ),
        },
        // This can happen if the structured data starts with UTF-8 characters,
        // e.g. the BOM, which is invalid at this position.
        None => bail!("First character after header fields must either be {NILVALUE} or ["),
    }

    const CLOSING_STRUCTURED_DATA_CHAR: char = ']';
    let mut idx = 0;
    let mut in_element = false;
    let mut escaping = false;
    for c in text.chars() {
        idx += 1;
        if in_element {
            if c == CLOSING_STRUCTURED_DATA_CHAR {
                in_element = escaping;
            }
        } else {
            match c {
                FIELD_SEPARATOR => {
                    // Decreasing the idx is ok here as we made sure above that there is at least
                    // one character that isn't a FIELD_SEPARATOR at the beginning of the string.
                    idx -= 1;
                    break;
                }
                OPENING_STRUCTURED_DATA_CHAR => in_element = true,
                _ => bail!("Unexpected character '{c}'. Expected [ or space"),
            }
        }
        escaping = !escaping && c == '\\';
    }

    let (structured_data, message) = text.split_at(idx);

    // We previously ensured that this segment is supposed to contain structured
    // data so this check doesn't treat a potential message as structured data.
    ensure!(
        structured_data.ends_with(CLOSING_STRUCTURED_DATA_CHAR),
        "Expected structured data but didn't find closing {CLOSING_STRUCTURED_DATA_CHAR}"
    );

    if message.is_empty() {
        Ok((structured_data, message))
    } else {
        // Remove the leading single space that's left over from splitting at
        // the index above.
        Ok((structured_data, &message[1..]))
    }
}

impl TryFrom<&str> for Rfc5424Message {
    type Error = anyhow::Error;

    // PRI VER SP TS SP HOST SP APP SP PROCID SP MSGID SP STRUCTD-DATA [SP MSG]
    // Header field:
    //   1   2     3       4      5         6        7
    // Array index:
    // 0          1     2       3      4         5        6
    fn try_from(data: &str) -> Result<Self> {
        const NUM_HEADER_FIELDS: usize = 6;
        // +1 here so we get the remaining content in the last element of the
        // vector.
        let mut fields: Vec<&str> = data
            .splitn(NUM_HEADER_FIELDS + 1, FIELD_SEPARATOR)
            .collect();

        let Some(remaining) = fields.pop() else {
            bail!("There must be enough data for header + structured data");
        };
        ensure!(
            fields.len() == NUM_HEADER_FIELDS,
            "Expecting {NUM_HEADER_FIELDS} fields in message header to form valid RFC 5424 message"
        );

        let mut header_field = fields.into_iter();
        let (facility, severity, version) =
            decode_pri(header_field.next().unwrap()).context("Failed to decode PRI field")?;
        ensure!(version == 1, "Only version 1 of RFC 5424 is supported");

        let timestamp =
            parse_timestamp(header_field.next().unwrap()).context("Failed to parse timestamp")?;

        // We're skipping the hostname.
        ensure_valid_field(header_field.next().unwrap(), "hostname", MaxLen(255))
            .context("Invalid hostname")?;

        let application_name = parse_application_name(header_field.next().unwrap())
            .context("Failed to parse application name")?;

        // We're skipping the PROCID.
        ensure_valid_field(header_field.next().unwrap(), "PROCID", MaxLen(128))
            .context("Invalid PROCID")?;

        // We're skipping the MSGID.
        ensure_valid_field(header_field.next().unwrap(), "MSGID", MaxLen(32))
            .context("Invalid MSGID")?;

        // We're skipping the structured data, it's validated inside the function already.
        let (_, message) = split_structured_data_and_message(remaining)
            .context("Failed to split structured data and message")?;

        Ok(Rfc5424Message {
            application_name: application_name.into(),
            facility,
            message: strip_bom(message).into(),
            severity,
            timestamp,
        })
    }
}

#[cfg(test)]
#[path = "rfc5424_message_test.rs"]
mod tests;
