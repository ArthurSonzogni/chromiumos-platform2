// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use std::ffi::CStr;
use std::io::{Read, Write};
use std::process::Command;
use std::str::from_utf8;
use vsock::{get_local_cid, VsockAddr, VsockListener, VsockStream};

const SERVER_VSOCK_PORT: u32 = 3580;

const HEADER_SIZE: usize = 8;
const PAYLOAD_BUFFER_SIZE: usize = 4096;

const READ_CLIPBOARD_FROM_VM: u8 = 0;
const WRITE_CLIPBOARD_TYPE_EMPTY: u8 = 1;
const WRITE_CLIPBOARD_TYPE_TEXT_PLAIN: u8 = 2;

fn server_init() -> Result<VsockListener> {
    let cid = get_local_cid().context("Cannot get the local CID")?;
    let server_addr = VsockAddr::new(cid, SERVER_VSOCK_PORT);
    let listener = VsockListener::bind(&server_addr).context("Cannot listen the server")?;
    println!("Clipboard server listening on {:?}", server_addr);
    Ok(listener)
}

fn send_empty_data(stream: &mut VsockStream) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    header[0] = WRITE_CLIPBOARD_TYPE_EMPTY;
    stream.write(&header).context("Failed to write header")?;
    stream.flush().context("Failed to flush stream")
}

fn send_text_plain(stream: &mut VsockStream, data: &[u8]) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    header[0] = WRITE_CLIPBOARD_TYPE_TEXT_PLAIN;
    let data_size: u32 = data.len().try_into()?;
    header[4..8].copy_from_slice(&data_size.to_le_bytes());
    stream.write(&header).context("Failed to write header")?;
    stream.write(data).context("Failed to write payload data")?;
    stream.flush().context("Failed to flush stream")
}

fn handle_read_clipboard(stream: &mut VsockStream) -> Result<()> {
    let output = Command::new("wl-paste")
        .env("XDG_RUNTIME_DIR", "/run/chrome")
        .arg("--no-newline")
        .output()
        .context("Failed to execute command")?;
    if output.status.success() {
        println!(
            "Read from clipboard: {}",
            from_utf8(&output.stdout).context("Failed to convert stdout into string")?
        );
        send_text_plain(stream, &output.stdout)
    } else {
        println!(
            "Failed to read clipboard: {}",
            from_utf8(&output.stderr).context("Failed to convert stderr into string")?
        );
        send_empty_data(stream)
    }
}

fn handle_text_plain(stream: &mut VsockStream, size: usize) -> Result<()> {
    let mut buffer = [0; PAYLOAD_BUFFER_SIZE];
    stream.read(&mut buffer).context("Failed to read payload")?;
    // TODO(b/349702505): Handle when payload size is larger than PAYLOAD_BUFFER_SIZE.
    let text_data = CStr::from_bytes_until_nul(&buffer[..size])
        .context("Failed to convert payload data into CStr")?;
    let text_data = text_data
        .to_str()
        .context("Failed to convert payload data into CStr")?;

    let status = Command::new("wl-copy")
        .env("XDG_RUNTIME_DIR", "/run/chrome")
        .arg(text_data)
        .status()
        .context("Failed to execute command")?;
    if status.success() {
        println!("Copied plain text: {}", text_data);
    } else {
        println!("Failed to copy plain text");
    }
    Ok(())
}

fn handle_request(stream: &mut VsockStream) -> Result<()> {
    let mut header = [0; HEADER_SIZE];
    stream.read(&mut header).context("Failed to read header")?;
    let request_type = header[0];
    let payload_size = u32::from_le_bytes(header[4..8].try_into()?).try_into()?;
    match request_type {
        READ_CLIPBOARD_FROM_VM => handle_read_clipboard(stream),
        WRITE_CLIPBOARD_TYPE_TEXT_PLAIN => handle_text_plain(stream, payload_size),
        _ => Err(anyhow!("Unknown request type: {request_type:?}")),
    }
}

fn main() {
    let listener = server_init().expect("Failed to initialize clipboard sharing server");
    println!("Clipboard sharing server started");
    for stream in listener.incoming() {
        let mut stream = stream.expect("Invalid request from the client");
        handle_request(&mut stream).expect("Failed to handle the request from the client");
    }
}
