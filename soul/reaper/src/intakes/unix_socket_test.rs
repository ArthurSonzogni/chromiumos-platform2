// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use chrono::NaiveDate;
use tempfile::NamedTempFile;
use tokio::net::UnixDatagram;
use tokio::task::JoinHandle;
use tokio::time;

use crate::intakes::UnixSocket;
use crate::message::Message;
use crate::syslog::{Facility, Severity};
use crate::IntakeQueue;

struct Harness {
    _handle: JoinHandle<()>,
    queue: IntakeQueue,
    sender: UnixDatagram,
    _temp_file: NamedTempFile,
}

impl Harness {
    fn new(num_msg: usize) -> Self {
        let temp_file = NamedTempFile::new().unwrap();
        let queue = IntakeQueue::new(1).unwrap();
        let socket = UnixSocket::new_for_testing(queue.clone_writer(), temp_file.path());
        let sender = UnixDatagram::unbound().unwrap();
        sender.connect(temp_file.path()).unwrap();
        Harness {
            queue,
            _temp_file: temp_file,
            sender,
            _handle: tokio::spawn(async move {
                socket.listen_for_at_most_n_messages(num_msg).await.unwrap();
            }),
        }
    }
}

#[tokio::test]
async fn intake_queue_shutdown() {
    let temp_file = NamedTempFile::new().unwrap();
    let queue = IntakeQueue::new(1).unwrap();
    let socket = UnixSocket::new_for_testing(queue.clone_writer(), temp_file.path());
    let sender = UnixDatagram::unbound().unwrap();
    sender.connect(temp_file.path()).unwrap();
    drop(queue);
    let _handle = tokio::spawn(async move {
        assert_eq!(
            format!("{}", socket.listen().await.unwrap_err()),
            "Can't put messages into queue anymore: channel closed"
        );
    });
    sender.send(&['a' as u8; 1]).await.unwrap();
}

#[tokio::test]
async fn larger_than_buf_message() {
    let mut harness = Harness::new(2);

    const LARGER_THAN_BUF: usize = UnixSocket::BUF_SIZE + 1;
    harness.sender.send(&[0; LARGER_THAN_BUF]).await.unwrap();
    let message = harness.queue.next().await.unwrap();

    assert!(message.message.len() < LARGER_THAN_BUF);
    assert_eq!(message.message.len(), UnixSocket::BUF_SIZE);

    // Cutting of the last 0x80 creates an invalid UTF-8 string.
    let b = [0xee, 0x80, 0x80, 0xee, 0x80, 0x80];
    let cutoff_buf_size = UnixSocket::BUF_SIZE - b.len() + 1;
    let mut cutoff_buf = vec![0u8; cutoff_buf_size];
    cutoff_buf.extend_from_slice(&b);
    harness.sender.send(&cutoff_buf).await.unwrap();
    let cutoff_message = harness.queue.next().await.unwrap();

    assert!(!cutoff_message.message.ends_with(&b));
}

#[tokio::test]
async fn empty_message() {
    let mut harness = Harness::new(1);

    harness.sender.send(&[0u8; 0]).await.unwrap();
    let no_message = time::timeout(Duration::from_millis(1000), harness.queue.next()).await;

    assert_eq!(
        format!("{}", no_message.unwrap_err()),
        "deadline has elapsed"
    );
}

#[tokio::test]
async fn rfc3164_message() {
    let mut harness = Harness::new(2);

    let valid = "<0>Apr  1 12:34:56 localhost test3164 Sending to unix socket";
    harness.sender.send(valid.as_bytes()).await.unwrap();

    assert_eq!(
        harness.queue.next().await.unwrap(),
        Message {
            application_name: "test3164".into(),
            facility: Facility::Kern,
            message: (*b" Sending to unix socket").into(),
            severity: Severity::Emergency,
            timestamp: NaiveDate::from_ymd_opt(1970, 4, 1)
                .unwrap()
                .and_hms_opt(12, 34, 56)
                .unwrap()
                .and_utc(),
        }
    );

    // This message is "invalid", i.e. it doesn't follow the RFC 3164 guidelines, but will still
    // produce a message as any data received by the daemon has to be treated as a message.
    let invalid = "0>Apr  1 12:34:56 localhost test3164 Sending to unix socket";
    harness.sender.send(invalid.as_bytes()).await.unwrap();

    assert_eq!(
        harness.queue.next().await.unwrap(),
        Message {
            application_name: "".into(),
            facility: Facility::User,
            message: (*b"0>Apr  1 12:34:56 localhost test3164 Sending to unix socket").into(),
            severity: Severity::Notice,
            timestamp: NaiveDate::from_ymd_opt(1970, 1, 1)
                .unwrap()
                .and_hms_opt(0, 0, 0)
                .unwrap()
                .and_utc(),
        }
    );
}

#[tokio::test]
async fn rfc5142_message() {
    let mut harness = Harness::new(2);

    let valid =
        "<165>1 2003-08-24T05:14:15.000003-07:00 192.0.2.1 myproc 8710 - - %% It's time to make \
            the do-nuts.";
    harness.sender.send(valid.as_bytes()).await.unwrap();

    assert_eq!(
        harness.queue.next().await.unwrap(),
        Message {
            application_name: "myproc".into(),
            facility: Facility::Local4,
            message: (*b"%% It's time to make the do-nuts.").into(),
            severity: Severity::Notice,
            timestamp: chrono::DateTime::parse_from_rfc3339("2003-08-24T05:14:15.000003-07:00")
                .unwrap()
                .into(),
        }
    );

    let missing_structured_data_nil =
        "<165>1 2003-08-24T05:14:15.000003-07:00 192.0.2.1 myproc 8710 -  %% It's time to make \
            the do-nuts.";
    harness
        .sender
        .send(missing_structured_data_nil.as_bytes())
        .await
        .unwrap();

    assert_eq!(
        harness.queue.next().await.unwrap(),
        Message {
            application_name: "".into(),
            facility: Facility::Local4,
            message:
                (*b"1 2003-08-24T05:14:15.000003-07:00 192.0.2.1 myproc 8710 -  %% It's time to \
                 make the do-nuts.")
                    .into(),
            severity: Severity::Notice,
            timestamp: chrono::DateTime::UNIX_EPOCH,
        }
    );
}

#[tokio::test]
async fn utf16_data() {
    let mut harness = Harness::new(1);

    harness
        .sender
        .send(&[0xf7, 0xbf, 0xbf, 0xbf])
        .await
        .unwrap();
    assert_eq!(
        harness.queue.next().await.unwrap(),
        Message {
            application_name: "".into(),
            facility: Facility::User,
            message: [0xf7, 0xbf, 0xbf, 0xbf].into(),
            severity: Severity::Notice,
            timestamp: chrono::DateTime::UNIX_EPOCH,
        }
    );
}
