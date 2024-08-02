// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use std::process::Command;

fn dbus_send(
    destination_name: &str,
    destination_path: &str,
    method: &str,
    args: Option<&[&str]>,
) -> Result<String> {
    let mut command = Command::new("dbus-send");
    command.args([
        "--system",
        "--type=method_call",
        "--print-reply",
        "--fixed",
        format!("--dest={destination_name}").as_str(),
        destination_path,
        method,
    ]);
    if let Some(args) = args {
        command.args(args);
    }

    let output = command.output().context("Failed to execute dbus-send")?;
    if !output.status.success() {
        return Err(anyhow!("dbus {method}: {output:?}"));
    }
    let reply = String::from_utf8(output.stdout)
        .with_context(|| "dbus {method}: failed to convert reply")?;
    Ok(reply)
}

pub fn is_user_logged_in() -> Result<bool> {
    // TODO(b/348303697): Listen on signal:SessionStateChanged
    let reply = dbus_send(
        "org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        "org.chromium.SessionManagerInterface.RetrieveSessionState",
        None,
    )?;
    match reply.trim() {
        "started" => Ok(true),
        _ => Ok(false),
    }
}

pub fn is_screen_unlocked() -> Result<bool> {
    // TODO(b/348303697): Listen on signal:ScreenIsLocked signal:ScreenIsUnlocked
    let reply = dbus_send(
        "org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        "org.chromium.SessionManagerInterface.IsScreenLocked",
        None,
    )?;
    match reply.trim() {
        "false" => Ok(true),
        _ => Ok(false),
    }
}

pub fn send_open_url(url: &str) -> Result<()> {
    dbus_send(
        "org.chromium.UrlHandlerService",
        "/org/chromium/UrlHandlerService",
        "org.chromium.UrlHandlerServiceInterface.OpenUrl",
        Some(&[format!("string:{url}").as_str()]),
    )?;
    Ok(())
}
