// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use chrono::{Datelike, NaiveDate, TimeDelta, Utc};

use super::*;
use crate::syslog::message::*;

#[test]
fn rfc_examples() {
    let examples = [
        (
            "<34>Oct 11 22:14:15 mymachine su: 'su root' failed for lonvick on /dev/pts/8",
            Rfc3164Message {
                application_name: "su".into(),
                facility: Facility::Auth,
                message: (*b": 'su root' failed for lonvick on /dev/pts/8").into(),
                message_offset: 0,
                severity: Severity::Critical,
                timestamp: NaiveDate::from_ymd_opt(current_time().year(), 10, 11)
                    .unwrap()
                    .and_hms_opt(22, 14, 15)
                    .unwrap()
                    .and_utc(),
            },
        ),
        (
            "Use the BFG!",
            Rfc3164Message {
                message: (*b"Use the BFG!").into(),
                ..Default::default()
            },
        ),
        (
            "<13>Feb  5 17:32:18 10.0.0.99 Use the BFG!",
            Rfc3164Message {
                application_name: "Use".into(),
                facility: Facility::User,
                message: (*b" the BFG!").into(),
                message_offset: 0,
                severity: Severity::Notice,
                timestamp: NaiveDate::from_ymd_opt(current_time().year(), 2, 5)
                    .unwrap()
                    .and_hms_opt(17, 32, 18)
                    .unwrap()
                    .and_utc(),
            },
        ),
        (
            "<165>Aug 24 05:34:00 CST 1987 mymachine myproc[10]: %% It's \
         time to make the do-nuts.  %%  Ingredients: Mix=OK, Jelly=OK #\
         Devices: Mixer=OK, Jelly_Injector=OK, Frier=OK # Transport:\
         Conveyer1=OK, Conveyer2=OK # %%",
            Rfc3164Message {
                application_name: "1987".into(),
                facility: Facility::Local4,
                message: (*b" mymachine myproc[10]: %% It's \
         time to make the do-nuts.  %%  Ingredients: Mix=OK, Jelly=OK #\
         Devices: Mixer=OK, Jelly_Injector=OK, Frier=OK # Transport:\
         Conveyer1=OK, Conveyer2=OK # %%")
                    .into(),
                message_offset: 0,
                severity: Severity::Notice,
                timestamp: NaiveDate::from_ymd_opt(current_time().year(), 8, 24)
                    .unwrap()
                    .and_hms_opt(5, 34, 0)
                    .unwrap()
                    .and_utc(),
            },
        ),
        (
            "<0>1990 Oct 22 10:52:01 TZ-6 scapegoat.dmz.example.org 10.1.2.3 \
         sched[0]: That's All Folks!",
            Rfc3164Message {
                facility: Facility::Kern,
                message: (*b"1990 Oct 22 10:52:01 TZ-6 scapegoat.dmz.example.org 10.1.2.3 \
         sched[0]: That's All Folks!")
                    .into(),
                severity: Severity::Emergency,
                ..Default::default()
            },
        ),
        (
            "<0>Oct 22 10:52:12 scapegoat 1990 Oct 22 10:52:01 TZ-6 \
         scapegoat.dmz.example.org 10.1.2.3 sched[0]: That's All Folks!",
            Rfc3164Message {
                application_name: "1990".into(),
                facility: Facility::Kern,
                message: (*b" Oct 22 10:52:01 TZ-6 \
         scapegoat.dmz.example.org 10.1.2.3 sched[0]: That's All Folks!")
                    .into(),
                message_offset: 0,
                severity: Severity::Emergency,
                timestamp: NaiveDate::from_ymd_opt(current_time().year(), 10, 22)
                    .unwrap()
                    .and_hms_opt(10, 52, 12)
                    .unwrap()
                    .and_utc(),
            },
        ),
    ];

    for (input, expected) in examples {
        assert_eq!(Rfc3164Message::from(input.as_bytes()), expected);
    }
}

#[test]
fn invalid_message_segments() {
    let fixed_date = NaiveDate::from_ymd_opt(2024, 4, 1)
        .unwrap()
        .and_hms_opt(12, 34, 56)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(fixed_date);
    let cases = [
        (
            "Message too short for ts",
            "<0> foo bar",
            "String too short for timestamp",
            Rfc3164Message {
                facility: Facility::Kern,
                message: (*b" foo bar").into(),
                severity: Severity::Emergency,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "No more data after ts",
            "<0>Apr  1 12:34:56",
            "No more data after timestamp",
            Rfc3164Message {
                facility: Facility::Kern,
                severity: Severity::Emergency,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "Ts not followed by space",
            "<0>Apr  1 12:34:56_",
            "Timestamp must be followed by a space",
            Rfc3164Message {
                facility: Facility::Kern,
                message: (*b"_").into(),
                severity: Severity::Emergency,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "Invalid hostname",
            "<0>Apr  1 12:34:56 München su: foo bar",
            "Hostname must be valid ASCII %d32-126",
            Rfc3164Message {
                facility: Facility::Kern,
                message: "München su: foo bar".as_bytes().into(),
                severity: Severity::Emergency,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "Only one word after ts",
            "<0>Apr  1 12:34:56 foo",
            "Timestamp must be followed by hostname surrounded by spaces",
            Rfc3164Message {
                facility: Facility::Kern,
                message: (*b"foo").into(),
                severity: Severity::Emergency,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "Empty text",
            "",
            "Empty messages are invalid",
            Rfc3164Message::default(),
        ),
        (
            "Only text",
            "Something happened",
            "missing <",
            Rfc3164Message {
                facility: Facility::User,
                message: (*b"Something happened").into(),
                severity: Severity::Notice,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "Tag too long",
            "<13>Apr  1 12:34:56 localhost 123456789012345678901234567890123 foo",
            "No end of tag found in first 33 characters",
            Rfc3164Message {
                facility: Facility::User,
                message: (*b" 123456789012345678901234567890123 foo").into(),
                severity: Severity::Notice,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
        (
            "No tag",
            "<13>Apr  1 12:34:56 localhost TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQ=",
            "No end of tag found in first 33 characters",
            Rfc3164Message {
                facility: Facility::User,
                message: (*b" TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQ=").into(),
                severity: Severity::Notice,
                timestamp: fixed_date,
                ..Default::default()
            },
        ),
    ];

    for (context, input, output, state) in cases {
        let mut message = Rfc3164Message::default();
        assert_eq!(
            format!(
                "{}",
                message
                    .parse(input.as_bytes())
                    .unwrap_err()
                    .chain()
                    .last()
                    .unwrap()
            ),
            output,
            "{context}"
        );
        assert_eq!(state, message, "{context}");
    }
}

#[test]
fn invalid_timestamp() {
    let fixed_year = NaiveDate::from_ymd_opt(2024, 1, 1)
        .unwrap()
        .and_hms_opt(0, 0, 0)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(fixed_year);
    let cases = [
        (
            "No day of month",
            "Apr  1:11:11111",
            "First character after day of month must be a space",
        ),
        (
            "Space at beginning",
            " Apr 1 12:34:56",
            "Unrecognized month",
        ),
        ("Lower case month", "apr  1 12:34:56", "Unrecognized month"),
        (
            "Leading zero for day",
            "Apr 01 12:34:56",
            "Days must not have leading zeros",
        ),
        (
            "Wrong delimeter after month",
            "Apr_ 1 12:34:56",
            "First character after month must be a space",
        ),
        (
            "Wrong delimeter after day",
            "Apr  1_12:34:56",
            "First character after day of month must be a space",
        ),
        (
            "Day with char",
            "Apr x1 12:34:56",
            "invalid digit found in string",
        ),
        (
            "Too high hour",
            "Apr  1 25:34:56",
            "Couldn't create valid time from  25:34:56",
        ),
        (
            "Too high minute",
            "Apr  1 12:60:56",
            "Couldn't create valid time from  12:60:56",
        ),
        (
            "Leap seconds are not supported",
            "Apr  1 12:34:60",
            "Couldn't create valid time from  12:34:60",
        ),
        (
            "Space at day delimeter wrong location",
            "Apr 1  12:34:56",
            "invalid digit found in string",
        ),
        (
            "Wrong time delimeter",
            "Apr  1 12.34.56",
            "Couldn't find 3 elements separated by : for time parsing",
        ),
        (
            "Too short",
            "Apr  1 12:34",
            "RFC 3164 timestamp must be exactly 15 characters but is 12 characters long",
        ),
        (
            "Only English months are valid",
            "Mai  1 12:34:56",
            "Unrecognized month",
        ),
        (
            "Invalid ASCII character",
            "Apr\t 1 12:34:56",
            "Timestamp must only contain ASCII characters %d32-126",
        ),
        (
            "Too high day",
            "Apr 31 12:34:56",
            "Couldn't create valid day from 2024-4-31",
        ),
    ];
    for (context, input, output) in cases {
        assert_eq!(
            format!("{}", parse_timestamp(input).unwrap_err()),
            output,
            "{context}"
        );
    }
}

#[test]
fn valid_timestamp() {
    // This test skips on Dec 31st and leap years which get their own tests.
    let leap_year = NaiveDate::from_ymd_opt(2023, 1, 1)
        .unwrap()
        .and_hms_opt(0, 0, 0)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(leap_year);

    for days in 0..364 {
        let curr = leap_year + chrono::Days::new(days);
        assert_eq!(
            parse_timestamp(&format!("{}", curr.format("%b %e 12:34:56"))).unwrap(),
            curr.checked_add_signed(TimeDelta::seconds(12 * 60 * 60 + 34 * 60 + 56))
                .unwrap()
        );
    }

    for seconds in 0..60 * 60 * 24 {
        let curr = leap_year + core::time::Duration::from_secs(seconds);
        assert_eq!(
            parse_timestamp(&format!("{}", curr.format("Apr  1 %T"))).unwrap(),
            NaiveDate::from_ymd_opt(2023, 4, 1)
                .unwrap()
                .and_time(curr.time())
                .and_utc()
        );
    }
}

#[test]
fn leap_year() {
    let not_leap_year = NaiveDate::from_ymd_opt(2023, 1, 1)
        .unwrap()
        .and_hms_opt(0, 0, 0)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(not_leap_year);
    let feb_29th = "Feb 29 12:34:56";
    assert_eq!(
        format!("{}", parse_timestamp(feb_29th).unwrap_err()),
        "Couldn't create valid day from 2023-2-29"
    );
    let leap_year = not_leap_year.with_year(2024).unwrap();
    CURRENT_TIME.set(leap_year);
    let feb_29th = "Feb 29 12:34:56";
    assert_eq!(
        parse_timestamp(feb_29th).unwrap(),
        NaiveDate::from_ymd_opt(2024, 2, 29)
            .unwrap()
            .and_hms_opt(12, 34, 56)
            .unwrap()
            .and_utc()
    );
}

#[test]
fn new_year() {
    let new_year = NaiveDate::from_ymd_opt(Utc::now().year() + 1, 1, 1)
        .unwrap()
        .and_hms_opt(0, 0, 0)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(new_year);
    assert_eq!(
        parse_timestamp("Dec 31 23:59:59").unwrap(),
        NaiveDate::from_ymd_opt(current_time().year() - 1, 12, 31)
            .unwrap()
            .and_hms_opt(23, 59, 59)
            .unwrap()
            .and_utc()
    );
    assert_eq!(
        parse_timestamp("Dec  1 23:59:59").unwrap(),
        NaiveDate::from_ymd_opt(current_time().year(), 12, 1)
            .unwrap()
            .and_hms_opt(23, 59, 59)
            .unwrap()
            .and_utc()
    );
}

#[test]
fn valid_msg_parts() {
    let prefix = "<13>Apr  1 12:34:56 localhost";
    let fixed_date = NaiveDate::from_ymd_opt(2024, 4, 1)
        .unwrap()
        .and_hms_opt(12, 34, 56)
        .unwrap()
        .and_utc();
    CURRENT_TIME.set(fixed_date);
    let mut message = Rfc3164Message {
        facility: Facility::User,
        severity: Severity::Notice,
        timestamp: fixed_date,
        ..Default::default()
    };
    let mut valid = Rfc3164Message::default();
    valid
        .parse(&format!("{prefix} foo bar").as_bytes())
        .unwrap();
    message.message = (*b" bar").into();
    message.application_name = "foo".into();
    assert_eq!(valid, message);
    let mut min_tag = Rfc3164Message::default();
    min_tag
        .parse(&format!("{prefix} 1 foo").as_bytes())
        .unwrap();
    message.message = (*b" foo").into();
    message.application_name = "1".into();
    assert_eq!(min_tag, message);
    let mut max_tag = Rfc3164Message::default();
    max_tag
        .parse(&format!("{prefix} 12345678901234567890123456789012 foo").as_bytes())
        .unwrap();
    message.message = (*b" foo").into();
    message.application_name = "12345678901234567890123456789012".into();
    assert_eq!(max_tag, message);
    let mut unprintable_character = Rfc3164Message::default();
    unprintable_character
        .parse(&format!("{prefix} foo \twrong\tindent").as_bytes())
        .unwrap();
    message.message = (*b" \\twrong\\tindent").into();
    message.application_name = "foo".into();
    assert_eq!(unprintable_character, message);
    let mut grapheme_cluster = Rfc3164Message::default();
    grapheme_cluster
        .parse(&format!("{prefix} foo y̆").as_bytes())
        .unwrap();
    message.message = " y̆".as_bytes().into();
    message.application_name = "foo".into();
    assert_eq!(grapheme_cluster, message);
}

#[test]
fn non_utf8_parts() {
    testing_logger::setup();

    let parts = [
        "<13>",
        "Apr  1",
        "12:34:56",
        "localhost",
        "appname",
        "message",
    ];

    let utf16_data = vec![0xf7, 0xbf, 0xbf, 0xbf];

    for i in 0..parts.len() {
        let data_with_utf16 = parts
            .iter()
            .enumerate()
            .map(|(idx, elem)| {
                if idx == i {
                    utf16_data.as_slice()
                } else {
                    elem.as_bytes()
                }
            })
            .collect::<Vec<_>>()
            .join(vec![0x20].as_slice());
        assert!(Rfc3164Message::default().parse(&data_with_utf16).is_err());
        testing_logger::validate(|captured_logs| {
            assert_eq!(captured_logs.len(), 1);
            assert_eq!(
                captured_logs[0].body,
                format!(
                    "Failed to parse data after {} bytes as UTF-8. Skipping message validation \
                    for the remaining {} bytes",
                    parts.iter().take(i).join("").len() + i,
                    parts.iter().skip(i).join(" ").len() + utf16_data.len() - parts[i].len()
                )
            );
        });
    }
}
