// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use std::ffi::CStr;
use std::io::Read;
use std::process::Command;
use vsock::{get_local_cid, VsockAddr, VsockListener, VsockStream};

const SERVER_VSOCK_PORT: u32 = 3580;

const HEADER_SIZE: usize = 8;
const PAYLOAD_BUFFER_SIZE: usize = 4096;

const WRITE_CLIPBOARD_TYPE_TEXT_PLAIN: u8 = 2;

fn server_init() -> Result<VsockListener> {
    let cid = get_local_cid().context("Cannot get the local CID")?;
    let server_addr = VsockAddr::new(cid, SERVER_VSOCK_PORT);
    let listener = VsockListener::bind(&server_addr).context("Cannot listen the server")?;
    println!("Clipboard server listening on {:?}", server_addr);
    Ok(listener)
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

    // TODO(b/351225383): Bring wl-copy binary into ChromeOS.
    let status = Command::new("wl-copy")
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
        WRITE_CLIPBOARD_TYPE_TEXT_PLAIN => handle_text_plain(stream, payload_size),
        _ => Err(anyhow!("Unknown request type: {request_type:?}")),
    }
}

fn main() {
    let listener = server_init().expect("Failed to initialize clipboard sharing server");
    for stream in listener.incoming() {
        let mut stream = stream.expect("Invalid request from the client");
        handle_request(&mut stream).expect("Failed to handle the request from the client");
    }
    println!("Hello, ferrochrome clipboard!");
}
