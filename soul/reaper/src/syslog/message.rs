// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, ensure, Context, Result};
use chrono::{DateTime, Utc};

pub use crate::syslog::{Facility, Severity};

pub trait SyslogMessage: Send {
    fn application_name(&self) -> &str;
    fn facility(&self) -> Facility;
    fn message(&self) -> &str;
    fn severity(&self) -> Severity;
    fn timestamp(&self) -> DateTime<Utc>;
}

/// Parse a `Pri` from `text` and return the string after the `Pri`.
pub(super) fn parse_pri(text: &str) -> Result<(Pri, &str)> {
    let (num_str, message) = text
        .strip_prefix('<')
        .context("missing <")?
        .split_once('>')
        .context("missing >")?;
    let pri_num: u8 = num_str.parse().context("invalid PRI number")?;
    if num_str.len() > 1 && num_str.starts_with('0') {
        bail!("invalid leading zero in PRI number");
    }
    Ok((Pri::try_from(pri_num)?, message))
}

/// PRI is a calculated priority value in syslog packets.
///
/// PRI is a combination of `Facility` and `Severity` into a numeric value.
/// The same value is used for RFC 3164 and 5424 messages.
#[derive(Debug, PartialEq)]
pub(super) struct Pri(u8);

impl TryFrom<u8> for Pri {
    type Error = anyhow::Error;
    fn try_from(v: u8) -> Result<Self> {
        ensure!(v <= 191, "{v} is too large for valid PRI. Max value is 191");
        Ok(Self(v))
    }
}

impl Pri {
    pub fn facility(&self) -> Facility {
        // We check in `try_from` that the number is at most 191. 191 / 8 is 23, i.e. the Local7
        // facility. The internal number of Pri is inaccessible to other parts of the code so it
        // isn't possible to directly construct one with an invalid value without going through
        // `try_from` first.
        Facility::try_from(self.0 / 8).expect("Valid PRI should be divisible by 8 into a Facility")
    }

    pub fn severity(&self) -> Severity {
        // The largest number from % 8 can be 7 which does correspond to the Debug severity.
        Severity::try_from(self.0 % 8)
            .expect("Valid PRI should have a valid remainder identifying a Severity")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_pri_from_text() {
        let errors = [
            ("No <", "1>", "missing <"),
            ("No >", "<1", "missing >"),
            ("Number larger than u8", "<256>", "invalid PRI number"),
            ("Not a number between <>", "<a>", "invalid PRI number"),
            ("Leading 0", "<01>", "invalid leading zero in PRI number"),
        ];

        for (context, input, err) in errors {
            assert_eq!(
                format!("{}", parse_pri(input).unwrap_err()),
                err,
                "{}",
                context
            );
        }

        assert_eq!(parse_pri("<1>").unwrap(), (Pri::try_from(1).unwrap(), ""));
        assert_eq!(parse_pri("<0>1").unwrap(), (Pri::try_from(0).unwrap(), "1"));
        assert_eq!(
            parse_pri("<0>Apr").unwrap(),
            (Pri::try_from(0).unwrap(), "Apr")
        );
    }

    #[test]
    fn invalid_pri() {
        let cases = [
            (
                "Pri with too large number",
                "<192>Apr  1 12:34:56 localhost foo: Bar",
                "192 is too large for valid PRI. Max value is 191",
            ),
            (
                "Pri with negative num",
                "<-1>Apr  1 12:34:56 localhost foo: Bar",
                "invalid PRI number",
            ),
            (
                "Pri with leading zero",
                "<01>Apr  1 12:34:56 localhost foo: Bar",
                "invalid leading zero in PRI number",
            ),
            (
                "Leading space",
                " <1>Apr  1 12:34:56 localhost foo: Bar",
                "missing <",
            ),
            (
                "Pri with wrong brackets",
                "[1]Apr  1 12:34:56 localhost foo: Bar",
                "missing <",
            ),
            (
                "Pri with wrong closing brackets",
                "<1]Apr  1 12:34:56 localhost foo: Bar",
                "missing >",
            ),
        ];

        for (context, input, output) in cases {
            assert_eq!(
                format!("{}", parse_pri(input).unwrap_err()),
                output,
                "{context}"
            );
        }
    }

    #[test]
    fn valid_pri() {
        for num in 0..=191 {
            let input = format!("<{num}>");
            let (pri, remaining) = parse_pri(&input).unwrap();
            assert!(remaining.is_empty());
            assert_eq!(pri.facility(), Facility::try_from(num / 8).unwrap());
            assert_eq!(pri.severity(), Severity::try_from(num % 8).unwrap());
        }
    }
}
