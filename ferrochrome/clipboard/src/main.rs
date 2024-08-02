// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod dbus_bindings;

use anyhow::{anyhow, Context, Result};
use libchromeos::syslog;
use log::{error, info, warn};
use std::ffi::CStr;
use std::io::{Read, Write};
use std::process::Command;
use std::str::from_utf8;
use tokio::sync::mpsc;
use tokio::time::{sleep, Duration};
use vsock::{get_local_cid, VsockAddr, VsockListener, VsockStream};

// Program name.
const IDENT: &str = "clipboard_sharing_server";

const SERVER_VSOCK_PORT: u32 = 3580;

const HEADER_SIZE: usize = 8;

const READ_CLIPBOARD_FROM_VM: u8 = 0;
const WRITE_CLIPBOARD_TYPE_EMPTY: u8 = 1;
const WRITE_CLIPBOARD_TYPE_TEXT_PLAIN: u8 = 2;
const OPEN_URL: u8 = 3;

fn server_init() -> Result<VsockListener> {
    let cid = get_local_cid().context("Cannot get the local CID")?;
    let server_addr = VsockAddr::new(cid, SERVER_VSOCK_PORT);
    let listener = VsockListener::bind(&server_addr).context("Cannot listen the server")?;
    info!("Clipboard server listening on {:?}", server_addr);
    Ok(listener)
}

fn send_empty_data(stream: &mut VsockStream) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    header[0] = WRITE_CLIPBOARD_TYPE_EMPTY;
    stream.write_all(&header).context("Failed to write header")?;
    stream.flush().context("Failed to flush stream")
}

fn send_text_plain(stream: &mut VsockStream, data: &[u8]) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    header[0] = WRITE_CLIPBOARD_TYPE_TEXT_PLAIN;
    let data_size: u32 = data.len().try_into()?;
    header[4..8].copy_from_slice(&data_size.to_le_bytes());
    stream.write_all(&header).context("Failed to write header")?;
    stream.write_all(data).context("Failed to write payload data")?;
    stream.flush().context("Failed to flush stream")
}

fn handle_read_clipboard(stream: &mut VsockStream) -> Result<()> {
    let output = Command::new("wl-paste")
        .env("XDG_RUNTIME_DIR", "/run/chrome")
        .arg("--no-newline")
        .output()
        .context("Failed to execute command")?;
    if output.status.success() {
        info!(
            "Read from clipboard: {}",
            from_utf8(&output.stdout).context("Failed to convert stdout into string")?
        );
        send_text_plain(stream, &output.stdout)
    } else {
        info!(
            "Failed to read clipboard: {}",
            from_utf8(&output.stderr).context("Failed to convert stderr into string")?
        );
        send_empty_data(stream)
    }
}

fn handle_text_plain(stream: &mut VsockStream, size: usize) -> Result<()> {
    let mut buffer = vec![0; size];
    stream.read_exact(&mut buffer).context("Failed to read payload")?;
    let text_data =
        CStr::from_bytes_with_nul(&buffer).context("Failed to convert payload data into CStr")?;
    let text_data = text_data.to_str().context("Failed to convert payload data into CStr")?;

    let status = Command::new("wl-copy")
        .env("XDG_RUNTIME_DIR", "/run/chrome")
        .arg(text_data)
        .status()
        .context("Failed to execute command")?;
    if status.success() {
        info!("Copied plain text: {}", text_data);
    } else {
        error!("Failed to copy plain text");
    }
    Ok(())
}

fn handle_open_url(
    stream: &mut VsockStream,
    size: usize,
    url_sender: mpsc::Sender<String>,
) -> Result<()> {
    let mut buffer = vec![0; size];
    stream.read_exact(&mut buffer).context("Failed to read payload")?;
    let url = String::from_utf8(buffer)?;
    url_sender.try_send(url).context("Failed to enqueue the URL channel")
}

fn handle_request(stream: &mut VsockStream, url_sender: mpsc::Sender<String>) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    stream.read_exact(&mut header).context("Failed to read header")?;
    let request_type = header[0];
    let payload_size = u32::from_le_bytes(header[4..8].try_into()?).try_into()?;
    match request_type {
        READ_CLIPBOARD_FROM_VM => handle_read_clipboard(stream),
        WRITE_CLIPBOARD_TYPE_TEXT_PLAIN => handle_text_plain(stream, payload_size),
        OPEN_URL => handle_open_url(stream, payload_size, url_sender),
        _ => Err(anyhow!("Unknown request type: {request_type:?}")),
    }
}

fn is_user_logged_in_and_unlocked() -> Result<bool> {
    Ok(dbus_bindings::is_user_logged_in()? && dbus_bindings::is_screen_unlocked()?)
}

async fn wait_user_logged_in_and_unlocked() {
    // TODO(b/348303697): Listen on dbus signals instead of polling.
    // Don't be too spammy. Log only the first few messages.
    let mut should_log_error = true;
    loop {
        match is_user_logged_in_and_unlocked() {
            Ok(true) => break,
            Ok(false) => {
                if should_log_error {
                    info!("Still waiting for user logged in and unlocked");
                }
            }
            Err(e) => {
                if should_log_error {
                    error!("Error querying user logged in and unlocked: {e:?}");
                }
            }
        }
        should_log_error = false;
        sleep(Duration::from_secs(1)).await;
    }
}

fn obfuscate_text(text: &str, plain_length: usize) -> String {
    let plain: String = text.chars().take(plain_length).collect();
    let utf8_len = text.chars().count();
    let obfuscated =
        if utf8_len > plain_length { format!("***** (len={utf8_len})") } else { "".to_string() };
    format!("{plain}{obfuscated}")
}

async fn process_open_url(mut url_receiver: mpsc::Receiver<String>) {
    while let Some(url) = url_receiver.recv().await {
        wait_user_logged_in_and_unlocked().await;
        info!("User session is started and unlocked");
        match dbus_bindings::send_open_url(&url) {
            Err(e) => warn!("{e:?}"),
            Ok(_) => info!("Sent OpenUrl dbus message: {}", obfuscate_text(&url, 17)),
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    syslog::init(IDENT.to_string(), true /* log_to_stderr */).expect("Failed to initialize logger");
    let listener = server_init()
        .inspect_err(|e| error!("Failed to initialize clipboard sharing server: {e:?}"))?;

    let (url_sender, url_receiver) = mpsc::channel::<String>(10);
    tokio::spawn(process_open_url(url_receiver));

    for stream in listener.incoming() {
        match stream {
            Ok(mut stream) => {
                let url_sender = url_sender.clone();
                tokio::spawn(async move {
                    if let Err(e) = handle_request(&mut stream, url_sender) {
                        error!("Failed to handle the request from the client: {e:?}");
                    }
                });
            }
            Err(e) => {
                error!("Stream is invalid: {e:?}");
            }
        }
    }
    Ok(())
}
