// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{ensure, Result};
use tokio::sync::mpsc::error::SendError;
use tokio::sync::mpsc::{channel, Receiver, Sender};

use crate::message::Message;

/// The writer interface for the intake queue.
///
/// This is supposed to be the sole input method for the queue, except for
/// modifying the queue directly.
/// The structure hides away any non-supported mechanisms of writing to the
/// queue that may otherwise be exposed by the underlying mechanism.
#[derive(Clone, Debug)]
pub struct Writer(Sender<Message>);

impl Writer {
    pub async fn send(&self, message: Message) -> core::result::Result<(), SendError<Message>> {
        self.0.send(message).await
    }
}

/// A FIFO queue for all messages received by `reaper`.
///
/// The queue ensures that all messages regardless of which interface received
/// them are processed the same way.
///
/// # Example
/// ```
/// let mut in_queue = IntakeQueue::new(10);
///
/// let some_interface = SomeInterface::new(in_queue.clone_writer());
/// thread::spawn(move || some_interface.run());
///
/// while let Some(message) = in_queue.pop() {
///     println!("New message from {}", message.application_name)'
/// }
/// ```
#[derive(Debug)]
pub struct IntakeQueue {
    reader: Receiver<Message>,
    writer: Writer,
}

impl IntakeQueue {
    /// Create a new queue with the specified `size`.
    ///
    /// The queue will cache up to `size` messages non-blocking. After the
    /// capacity is exhausted any further `push` or `send` calls will block
    /// until there is at least 1 free capacity. This also means that a `size`
    /// of `0` is invalid.
    pub fn new(size: usize) -> Result<Self> {
        ensure!(size > 0, "Queue size must be larger than 0");
        let (writer, reader) = channel(size);

        Ok(IntakeQueue {
            reader,
            writer: Writer(writer),
        })
    }

    /// Get a clone of the writer for this queue.
    ///
    /// The cloned writer still writes to the same queue, calling this function
    /// doesn't lead to cloning the remaining data structures or creating a new
    /// queue.
    /// Cloned writers are useful in contexts where moving is required, e.g.
    /// into other async/thread contexts to write into a single queue from
    /// multiple places.
    pub fn clone_writer(&self) -> Writer {
        self.writer.clone()
    }

    /// Receive the next message from the queue.
    ///
    /// Takes the current first message if it exists or will return one once
    /// there is a message.
    pub async fn next(&mut self) -> Option<Message> {
        self.reader.recv().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::time::Duration;

    use tokio::time::timeout;

    use crate::message::tests::new_test_message;

    #[test]
    fn invalid_queue_size() {
        assert_eq!(
            format!("{}", IntakeQueue::new(0).unwrap_err()),
            "Queue size must be larger than 0"
        );
    }

    #[tokio::test]
    async fn read_empty() {
        let mut queue = IntakeQueue::new(1).unwrap();

        assert_eq!(
            format!(
                "{}",
                timeout(Duration::from_millis(10), queue.next())
                    .await
                    .unwrap_err()
            ),
            "deadline has elapsed"
        );
    }

    #[tokio::test]
    async fn async_read_one_message() {
        let mut queue = IntakeQueue::new(1).unwrap();

        queue.clone_writer().send(new_test_message()).await.unwrap();

        assert_ne!(queue.next().await, None);

        assert_eq!(
            format!(
                "{}",
                timeout(Duration::from_millis(10), queue.next())
                    .await
                    .unwrap_err()
            ),
            "deadline has elapsed"
        );
    }

    #[tokio::test]
    async fn reach_queue_capacity() {
        let mut queue = IntakeQueue::new(1).unwrap();

        let writer = queue.clone_writer();

        writer.0.try_send(new_test_message()).unwrap();
        assert!(writer.0.try_send(new_test_message()).is_err());
        assert_ne!(queue.next().await, None);
        writer.0.try_send(new_test_message()).unwrap();
    }
}
