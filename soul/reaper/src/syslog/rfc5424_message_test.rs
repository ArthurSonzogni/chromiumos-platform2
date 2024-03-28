// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::syslog::message::*;
use crate::syslog::rfc5424_message::*;

use chrono::{FixedOffset, NaiveDate};

const VALID_FIELD_SEQUENCE: [&str; 8] = [
    "<42>1",
    "2024-04-01T12:34:56.789Z",
    "localhost",
    "test",
    "1234",
    "TEST_MSG",
    "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\" eventID=\"1011\"]",
    "Foo bar",
];

#[test]
fn empty_fields_that_must_not_be_empty() {
    for i in 0..6 {
        let sequence_with_empty_field = VALID_FIELD_SEQUENCE
            .iter()
            .enumerate()
            .map(|(idx, elem)| if idx == i { "" } else { elem })
            .collect::<Vec<_>>()
            .join(" ");

        assert!(format!(
            "{}",
            Rfc5424Message::try_from(sequence_with_empty_field.as_str())
                .unwrap_err()
                .chain()
                .last()
                .unwrap()
        )
        .ends_with("must not be empty"));
    }
}

#[test]
fn fields_too_long() {
    for (i, too_big) in [8, 33, 256, 49, 129, 33].into_iter().enumerate() {
        let long_data = vec![b'a'; too_big];
        let too_long_field = VALID_FIELD_SEQUENCE
            .iter()
            .enumerate()
            .map(|(idx, elem)| {
                if idx == i {
                    std::str::from_utf8(&long_data).unwrap()
                } else {
                    elem
                }
            })
            .collect::<Vec<_>>()
            .join(" ");

        assert!(format!(
            "{}",
            Rfc5424Message::try_from(too_long_field.as_str())
                .unwrap_err()
                .chain()
                .last()
                .unwrap()
        )
        .contains("must be at most"));
    }
}

#[test]
fn fields_contain_non_print_ascii() {
    for i in 0..6 {
        let with_non_print_field = VALID_FIELD_SEQUENCE
            .iter()
            .enumerate()
            .map(|(idx, elem)| if idx == i { "\u{ffff}" } else { elem })
            .collect::<Vec<_>>()
            .join(" ");

        assert!(format!(
            "{}",
            Rfc5424Message::try_from(with_non_print_field.as_str())
                .unwrap_err()
                .chain()
                .last()
                .unwrap()
        )
        .ends_with("must be valid ASCII [33,126]"));
    }
}

#[test]
fn not_enough_fields() {
    let too_few_fields = VALID_FIELD_SEQUENCE
        .iter()
        .copied()
        .take(6)
        .collect::<Vec<_>>()
        .join(" ");

    assert_eq!(
        format!(
            "{}",
            Rfc5424Message::try_from(too_few_fields.as_str())
                .unwrap_err()
                .chain()
                .last()
                .unwrap()
        ),
        "Expecting 6 fields in message header to form valid RFC 5424 message"
    );

    let no_space = "foo";
    assert_eq!(
        format!(
            "{}",
            Rfc5424Message::try_from(no_space)
                .unwrap_err()
                .chain()
                .last()
                .unwrap()
        ),
        "Expecting 6 fields in message header to form valid RFC 5424 message"
    );
}

#[test]
fn application_name() {
    let nil = NILVALUE;

    assert_eq!(parse_application_name(nil).unwrap(), "");
    assert_eq!(parse_application_name("foo").unwrap(), "foo");
}

#[test]
fn invalid_version() {
    let max_pri_v2 = "<191>2";
    assert_eq!(
        decode_pri(max_pri_v2).unwrap(),
        (Facility::Local7, Severity::Debug, 2)
    );
    let negative_version = "<0>-1";
    assert_eq!(
        format!("{}", decode_pri(negative_version).unwrap_err()),
        "invalid digit found in string"
    );
    let pri_without_angle_bracket = "0>-1";
    assert_eq!(
        format!("{}", decode_pri(pri_without_angle_bracket).unwrap_err()),
        "missing <"
    );
    let too_long_version = "<191>001";
    assert_eq!(
        format!("{}", decode_pri(too_long_version).unwrap_err()),
        "PRI and version must be at most 7 ASCII characters"
    );
    let version_char = "<0>a";
    assert_eq!(
        format!("{}", decode_pri(version_char).unwrap_err()),
        "invalid digit found in string"
    );
}

#[test]
fn rfc_examples() {
    let bom = [0xEF, 0xBB, 0xBF];
    let example_with_bom = format!(
        "<34>1 2003-10-11T22:14:15.003Z mymachine.example.com su - ID47 - \
            {}'su root' failed for lonvick on /dev/pts/8",
        std::str::from_utf8(&bom).unwrap()
    );
    assert_eq!(
        Rfc5424Message {
            application_name: "su".into(),
            facility: Facility::Auth,
            message: "'su root' failed for lonvick on /dev/pts/8".into(),
            severity: Severity::Critical,
            timestamp: NaiveDate::from_ymd_opt(2003, 10, 11)
                .unwrap()
                .and_hms_milli_opt(22, 14, 15, 3)
                .unwrap()
                .and_utc(),
        },
        Rfc5424Message::try_from(example_with_bom.as_str()).unwrap()
    );

    const SECONDS_IN_HOUR: i32 = 3600;
    let example_without_structured_data = "<165>1 2003-08-24T05:14:15.000003-07:00 192.0.2.1 \
         myproc 8710 - - %% It's time to make the do-nuts.";
    assert_eq!(
        Rfc5424Message {
            application_name: "myproc".into(),
            facility: Facility::Local4,
            message: "%% It's time to make the do-nuts.".into(),
            severity: Severity::Notice,
            timestamp: NaiveDate::from_ymd_opt(2003, 8, 24)
                .unwrap()
                .and_hms_micro_opt(5, 14, 15, 3)
                .unwrap()
                .and_local_timezone(FixedOffset::west_opt(7 * SECONDS_IN_HOUR).unwrap())
                .unwrap()
                .to_utc(),
        },
        Rfc5424Message::try_from(example_without_structured_data).unwrap()
    );

    let example_with_bom_and_structured_data = format!(
        "<165>1 2003-10-11T22:14:15.003Z \
        mymachine.example.com evntslog - ID47 [exampleSDID@32473 iut=\"3\" \
        eventSource=\"Application\" eventID=\"1011\"] {}An application \
           event log entry",
        std::str::from_utf8(&bom).unwrap()
    );
    assert_eq!(
        Rfc5424Message {
            application_name: "evntslog".into(),
            facility: Facility::Local4,
            message: "An application event log entry".into(),
            severity: Severity::Notice,
            timestamp: NaiveDate::from_ymd_opt(2003, 10, 11)
                .unwrap()
                .and_hms_milli_opt(22, 14, 15, 3)
                .unwrap()
                .and_utc(),
        },
        Rfc5424Message::try_from(example_with_bom_and_structured_data.as_str()).unwrap()
    );

    let example_only_structured_data = "<165>1 2003-10-11T22:14:15.003Z \
        mymachine.example.com evntslog - ID47 [exampleSDID@32473 iut=\"3\" \
        eventSource=\"Application\" eventID=\"1011\"][examplePriority@32473 \
           class=\"high\"]";
    assert_eq!(
        Rfc5424Message {
            application_name: "evntslog".into(),
            facility: Facility::Local4,
            message: "".into(),
            severity: Severity::Notice,
            timestamp: NaiveDate::from_ymd_opt(2003, 10, 11)
                .unwrap()
                .and_hms_milli_opt(22, 14, 15, 3)
                .unwrap()
                .and_utc(),
        },
        Rfc5424Message::try_from(example_only_structured_data).unwrap()
    );
}

#[test]
fn parse_invalid_rfc_5424_timestamp() {
    assert_eq!(
        format!("{}", parse_timestamp("").unwrap_err(),),
        "timestamp must not be empty"
    );

    let unprintable_ascii = 32 as char;
    assert_eq!(
        format!(
            "{}",
            parse_timestamp(&unprintable_ascii.to_string()).unwrap_err(),
        ),
        "timestamp must be valid ASCII [33,126]"
    );

    let too_long_data = "YYYY-MM-DDTHH:MM:SS.ffffff+HH:MM:(";
    assert_eq!(
        format!("{}", parse_timestamp(too_long_data).unwrap_err(),),
        "timestamp must be at most 32 ASCII characters"
    );

    let too_short_data = "YYYY-MM-DDTHH:MM:SS";
    assert_eq!(
        format!("{}", parse_timestamp(too_short_data).unwrap_err(),),
        "Timestamp is too short to be valid RFC 3339"
    );

    let timestamp_with_lower_t = "2024-04-01t12:34:56Z";
    assert_eq!(
        format!("{}", parse_timestamp(timestamp_with_lower_t).unwrap_err(),),
        "RFC 5424 must contain upper case T between date and time"
    );

    let timestamp_with_leap_second = "2024-04-01T12:34:60Z";
    assert_eq!(
        format!(
            "{}",
            parse_timestamp(timestamp_with_leap_second).unwrap_err(),
        ),
        "Leap seconds must not be used in RFC 5424 timestamps"
    );

    let timestamp_with_nanoseconds = "2024-04-01T12:34:59.123456789Z";
    assert_eq!(
        format!(
            "{}",
            parse_timestamp(timestamp_with_nanoseconds).unwrap_err(),
        ),
        "timestamp must not contain more than 6 fractional digits of a second"
    );

    let timestamp_with_lower_z = "2024-04-01T12:34:59z";
    assert_eq!(
        format!("{}", parse_timestamp(timestamp_with_lower_z).unwrap_err(),),
        "RFC 5424 timestamps must end in upper case Z or numerical offset"
    );
}

#[test]
fn extract_structured_data_and_messages_valid() {
    let cases = [
        (
            "Valid structured data without a message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"]",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"]",
            "",
        ),
        (
            "Valid structured data and a message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"] foo",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"]",
            "foo",
        ),
        (
            "Valid structured data with escaped ] but no message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"]",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"]",
            "",
        ),
        (
            "Valid structured data with escaped ] with message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"] foo",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"]",
            "foo",
        ),
        (
            "Valid structured data with escaped ] with message containig \
                             additional FIELD_SEPARATOR",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"] foo bar",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\]\"
                   eventID=\"1011\"]",
            "foo bar",
        ),
        (
            "Valid structured data where multiple elements are separate \
                 by FIELD_SEPARATOR and thus the second SD element is treated \
                as a message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"] [examplePriority@32473 class=\"high\"]",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"]",
            "[examplePriority@32473 class=\"high\"]",
        ),
        (
            "Multiple structured data elements without a message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"][examplePriority@32473 class=\"high\"]",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"][examplePriority@32473 class=\"high\"]",
            "",
        ),
        (
            "Multiple structured data elements witha message",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"][examplePriority@32473 class=\"high\"] foo",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"Application\"
           eventID=\"1011\"][examplePriority@32473 class=\"high\"]",
            "foo",
        ),
        ("No structured data and no message", "-", "", ""),
        ("No structured data and an empty message", "- ", "", ""),
        ("No structured data and a message", "- foo", "", "foo"),
    ];

    for (context, input, structured_data, message) in cases {
        assert_eq!(
            split_structured_data_and_message(input).unwrap(),
            (structured_data, message),
            "{}",
            context
        );
    }
}

#[test]
fn extract_structured_data_and_messages_invalid() {
    let cases = [
        (
            "Empty data is invalid",
            "",
            "Valid RFC 5424 messages must have data after the header fields",
        ),
        (
            "Possibly message too long for some buffer or sender \
                quit before fully transmitted",
            "[exampleSDID@32473 iut=\"3\"",
            "Expected structured data but didn't find closing ]",
        ),
        (
            "CLI argument",
            "--foo=bar",
            "Hyphen for structured data must be followed by space \
                or nothing to be valid RFC 5424 message",
        ),
        (
            "BOM in structured data",
            std::str::from_utf8(&[0xEF, 0xBB, 0xBF, 0x2D]).unwrap(),
            "First character after header fields must either be - or [",
        ),
        (
            "Invalid character at strucutred data begin. Likely field isn't marked with -",
            "foo bar",
            "First character after header fields must either be - or [",
        ),
        (
            "Opening and closing character swapped",
            "]exampleSDID@32473 iut=\"3\"[",
            "First character after header fields must either be - or [",
        ),
        (
            "Closing character not followed by space",
            "[exampleSDID@32473 iut=\"3\"]foo",
            "Unexpected character 'f'. Expected [ or space",
        ),
        (
            "Closing character isn't escaped inside message because the backslash is escaped",
            "[exampleSDID@32473 iut=\"3\" eventSource=\"[Application\\\\]\"
                   eventID=\"1011\"]",
            "Unexpected character '\"'. Expected [ or space",
        ),
    ];

    for (context, input, err) in cases {
        assert_eq!(
            format!("{}", split_structured_data_and_message(input).unwrap_err()),
            err,
            "{}",
            context
        );
    }
}
