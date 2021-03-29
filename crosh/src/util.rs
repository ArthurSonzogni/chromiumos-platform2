// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides helper functions used by handler implementations of crosh commands.

use std::env;
use std::error;
use std::fmt::{self, Display};
use std::fs::read_to_string;
use std::io::Read;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use dbus::blocking::Connection;
use libc::c_int;
use libchromeos::chromeos;
use regex::Regex;
use sys_util::{clear_signal_handler, error, register_signal_handler};

// 25 seconds is the default timeout for dbus-send.
pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

const CROS_USER_ID_HASH: &str = "CROS_USER_ID_HASH";

static INCLUDE_DEV: AtomicBool = AtomicBool::new(false);
static INCLUDE_USB: AtomicBool = AtomicBool::new(false);

pub enum Error {
    NoMatchFound,
    WrappedError(String),
}

impl Display for Error {
    #[remain::check]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        #[sorted]
        match self {
            NoMatchFound => write!(f, "No match found."),
            WrappedError(err) => write!(f, "{}", err),
        }
    }
}

impl<T: error::Error> From<T> for Error {
    fn from(err: T) -> Self {
        Error::WrappedError(format!("{:?}", err))
    }
}

// Return the user ID hash from the environment. If it is not available, fetch it from session
// manager and set the environment variable.
pub fn get_user_id_hash() -> Result<String, ()> {
    if let Ok(lookup) = env::var(CROS_USER_ID_HASH) {
        return Ok(lookup);
    }

    let user_id_hash = chromeos::get_user_id_hash().map_err(|err| {
        error!("ERROR: D-Bus call failed: {}", err);
    })?;

    env::set_var(CROS_USER_ID_HASH, &user_id_hash);
    Ok(user_id_hash)
}

pub fn is_chrome_feature_enabled(method_name: &str) -> Result<bool, ()> {
    let user_id_hash = get_user_id_hash()?;

    let connection = Connection::new_system().map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
    })?;

    let proxy = connection.with_proxy(
        "org.chromium.ChromeFeaturesService",
        "/org/chromium/ChromeFeaturesService",
        DEFAULT_DBUS_TIMEOUT,
    );

    let (reply,): (bool,) = proxy
        .method_call(
            "org.chromium.ChromeFeaturesServiceInterface",
            method_name,
            (user_id_hash,),
        )
        .map_err(|err| {
            error!("ERROR: D-Bus method call failed: {}", err);
        })?;

    Ok(reply)
}

pub fn is_removable() -> Result<bool, Error> {
    let dev = root_dev()?;
    let groups = Regex::new(r#"/dev/([^/]+?)p?[0-9]+$"#)?
        .captures(&dev)
        .ok_or(Error::NoMatchFound)?;

    let dev = groups.get(1).unwrap().as_str();

    match read_to_string(format!("/sys/block/{}/removable", dev)) {
        Ok(contents) => Ok(contents.trim() == "1"),
        Err(err) => Err(err.into()),
    }
}

pub fn set_dev_commands_included(value: bool) {
    INCLUDE_DEV.store(value, Ordering::Release);
}

pub fn set_usb_commands_included(value: bool) {
    INCLUDE_USB.store(value, Ordering::Release);
}

pub fn dev_commands_included() -> bool {
    INCLUDE_DEV.load(Ordering::Acquire)
}

pub fn usb_commands_included() -> bool {
    INCLUDE_USB.load(Ordering::Acquire)
}

/// Safety:
/// handler needs to be async safe.
pub unsafe fn set_signal_handlers(signums: &[c_int], handler: extern "C" fn(c_int)) {
    for signum in signums {
        if register_signal_handler(*signum, handler).is_err() {
            error!("sigaction failed for {}", signum);
        }
    }
}

pub fn clear_signal_handlers(signums: &[c_int]) {
    for signum in signums {
        if clear_signal_handler(*signum).is_err() {
            error!("sigaction failed for {}", signum);
        }
    }
}

fn root_dev() -> Result<String, Error> {
    let mut child = Command::new("rootdev")
        .arg("-s")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .spawn()?;

    let mut result = String::new();
    child.stdout.take().unwrap().read_to_string(&mut result)?;
    child.wait()?;

    Ok(result.trim().to_string())
}
