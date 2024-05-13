// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{bail, Context, Result};
use thiserror::Error as ThisError;
use tokio::net::UnixDatagram;

use crate::intake_queue::Writer;

#[derive(Debug, ThisError)]
enum ProcessMessageError {
    #[error("Received empty message")]
    EmptyMessage,
    #[error("Can't put messages into queue anymore: {0}")]
    QueueClosed(#[from] tokio::sync::mpsc::error::SendError<crate::message::Message>),
}

pub struct UnixSocket {
    socket: UnixDatagram,
    writer: Writer,
}

impl UnixSocket {
    // TODO(b/340529008) Check what the right buffer size is and how we can show users that a
    // message has been truncated.
    const BUF_SIZE: usize = 8192;

    fn create_socket(socket_path: &Path) -> Result<UnixDatagram> {
        // We and other tools don't set SO_REUSEADDR when creating the socket so we have to remove
        // it here before trying to bind to it again.
        if socket_path.exists() {
            std::fs::remove_file(socket_path).with_context(|| {
                format!("Couldn't remove {} to create socket", socket_path.display())
            })?;
        }

        Ok(UnixDatagram::bind(socket_path)
            .with_context(|| format!("Couldn't bind to socket {}", socket_path.display()))?)
    }

    pub fn new(writer: Writer) -> Result<Self> {
        let socket = Self::create_socket(Path::new("/dev/log"))
            .context("Couldn't create syslogd unix socket")?;
        Ok(UnixSocket { socket, writer })
    }

    #[cfg(test)]
    fn new_for_testing(writer: Writer, socket_path: &Path) -> Self {
        UnixSocket {
            socket: Self::create_socket(socket_path).unwrap(),
            writer,
        }
    }

    async fn listen_for_at_most_n_messages(&self, number_of_messages: usize) -> Result<()> {
        let mut buf = vec![0u8; Self::BUF_SIZE];

        for _ in 0..number_of_messages {
            let size = self
                .socket
                .recv(&mut buf)
                .await
                .context("Waiting for new messages")?;

            match self.process_message(&buf[..size]).await {
                Ok(()) => continue,
                Err(e) => Self::handle_processing_error(e)?,
            }
        }

        Ok(())
    }

    /// Listen for incoming messages.
    ///
    /// This doesn't listen endlessly but 'only' for `usize::MAX` messages. Afterwards this
    /// function will return and the function has to be called again if there are still messages to
    /// be received.
    pub async fn listen(&self) -> Result<()> {
        self.listen_for_at_most_n_messages(usize::MAX).await
    }

    fn handle_processing_error(err: ProcessMessageError) -> Result<()> {
        match err {
            ProcessMessageError::QueueClosed(e) => {
                bail!("Can't put messages into queue anymore: {e}")
            }
            _ => log::warn!("{err}"),
        }

        Ok(())
    }

    async fn process_message(&self, buf: &[u8]) -> Result<(), ProcessMessageError> {
        use crate::syslog::{Rfc3164Message, Rfc5424Message, SyslogMessage};

        use ProcessMessageError::*;

        if buf.is_empty() {
            return Err(EmptyMessage);
        }

        let message: Box<dyn SyslogMessage> = match Rfc5424Message::try_from(buf) {
            Ok(msg) => Box::new(msg),
            Err(err) => {
                log::trace!("Failed to parse message as RFC 5424 because: {err:#}");
                Box::new(Rfc3164Message::from(buf))
            }
        };

        self.writer.send(message.into()).await?;

        Ok(())
    }
}

#[cfg(test)]
#[path = "unix_socket_test.rs"]
mod tests;
