// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides helper functions used by handler implementations of crosh commands.

use rand::distributions::Alphanumeric;
use rand::{thread_rng, Rng};
use std::env;
use std::error;
use std::fmt::{self, Display};
use std::fs::read_to_string;
use std::io::stdin;
use std::io::stdout;
use std::io::Read;
use std::io::Write;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use chrono::Local;
use dbus::blocking::Connection;
use libc::{c_int, epoll_event};
use libchromeos::chromeos;
use libchromeos::signal::{clear_signal_handler, register_signal_handler};
use log::error;
use nix::sys::signal::Signal;
use system_api::client::OrgChromiumSessionManagerInterface;

// 25 seconds is the default timeout for dbus-send.
pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);
// Path to update_engine_client.
pub const UPDATE_ENGINE: &str = "/usr/bin/update_engine_client";

const CROS_USER_ID_HASH: &str = "CROS_USER_ID_HASH";

// The return value from device_management_client at install_attributes_get
// and the value is not set.
const DEVICE_MANAGEMENT_ERROR_INSTALL_ATTRIBUTES_GET_FAILED: i32 = 4;
// 30 seconds is the default timeout for epoll.
const DEFAULT_EPOLL_TIMEOUT: i32 = 30000;
const MAX_EPOLL_EVENTS: i32 = 1;

static INCLUDE_DEV: AtomicBool = AtomicBool::new(false);
static INCLUDE_USB: AtomicBool = AtomicBool::new(false);

#[derive(Debug)]
pub enum Error {
    DbusChromeFeaturesService(dbus::Error, String),
    DbusConnection(dbus::Error),
    DbusGetUserIdHash(chromeos::Error),
    DbusGuestSessionActive(dbus::Error),
    EpollAdd(i32),
    EpollCreate(i32),
    NoMatchFound,
    WrappedError(String),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Display for Error {
    #[remain::check]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        #[sorted]
        match self {
            DbusChromeFeaturesService(err, m) => write!(f, "failed to call '{}': {}", m, err),
            DbusConnection(err) => write!(f, "failed to connect to D-Bus: {}", err),
            DbusGetUserIdHash(err) => {
                write!(f, "failed to get user-id hash over to D-Bus: {:?}", err)
            }
            DbusGuestSessionActive(err) => {
                write!(
                    f,
                    "failed to check whether guest session is active over D-Bus: {:?}",
                    err
                )
            }
            EpollAdd(err) => write!(f, "failed to add to epoll: {}", err),
            EpollCreate(err) => write!(f, "failed to create epoll: {}", err),
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
pub fn get_user_id_hash() -> Result<String> {
    if let Ok(lookup) = env::var(CROS_USER_ID_HASH) {
        return Ok(lookup);
    }

    let user_id_hash = chromeos::get_user_id_hash().map_err(|err| {
        error!("ERROR: D-Bus call failed: {}", err);
        Error::DbusGetUserIdHash(err)
    })?;

    env::set_var(CROS_USER_ID_HASH, &user_id_hash);
    Ok(user_id_hash)
}

// Return the output file path for the given output type in user's Downloads directory.
pub fn generate_output_file_path(output_type: &str, file_extension: &str) -> Result<String> {
    let date = Local::now();
    let formatted_date = date.format("%Y-%m-%d_%H.%M.%S");
    let user_id_hash = get_user_id_hash()?;
    let random_string: String = thread_rng().sample_iter(&Alphanumeric).take(6).collect();
    Ok(format!(
        "/home/user/{}/MyFiles/Downloads/{}_{}_{}.{}",
        user_id_hash, output_type, formatted_date, random_string, file_extension
    ))
}

pub fn is_chrome_feature_enabled(method_name: &str) -> Result<bool> {
    let user_id_hash = get_user_id_hash()?;

    let connection = Connection::new_system().map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
        Error::DbusConnection(err)
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
            Error::DbusChromeFeaturesService(err, method_name.to_string())
        })?;

    Ok(reply)
}

pub fn is_no_new_privs_set() -> bool {
    // Safe because this retrieves a value without side effects.
    unsafe { libc::prctl(libc::PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) > 0 }
}

pub fn is_removable() -> Result<bool> {
    let dev = root_dev()?;
    let dev = blockdev_basename(&dev).ok_or(Error::NoMatchFound)?;
    match read_to_string(format!("/sys/block/{}/removable", dev)) {
        Ok(contents) => Ok(contents.trim() == "1"),
        Err(err) => Err(err.into()),
    }
}

pub fn is_consumer_device() -> Result<bool> {
    let output = Command::new("/usr/sbin/device_management_client")
        .arg("--action=install_attributes_get")
        .arg("--name=enterprise.mode")
        .output()?;

    let stdout = String::from_utf8(output.stdout).unwrap();

    // If the attribute is not set, device_management_client will treat it as an error, return
    // DEVICE_MANAGEMENT_ERROR_INSTALL_ATTRIBUTES_GET_FAILED and output nothing.
    match output.status.code() {
        Some(0) => Ok(!stdout.contains("enterprise")),
        Some(DEVICE_MANAGEMENT_ERROR_INSTALL_ATTRIBUTES_GET_FAILED) if stdout.is_empty() => {
            Ok(true)
        }
        None => Err(Error::WrappedError("failed to get exit code".to_string())),
        _ => Err(Error::WrappedError(stdout)),
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

/// # Safety
/// handler needs to be async safe.
pub unsafe fn set_signal_handlers(signums: &[Signal], handler: extern "C" fn(c_int)) {
    for signum in signums {
        // Safe as long as handler is async safe.
        if unsafe { register_signal_handler(*signum, handler) }.is_err() {
            error!("sigaction failed for {}", signum);
        }
    }
}

pub fn clear_signal_handlers(signums: &[Signal]) {
    for signum in signums {
        if clear_signal_handler(*signum).is_err() {
            error!("sigaction failed for {}", signum);
        }
    }
}

pub fn is_guest_session_active() -> Result<bool> {
    let connection = Connection::new_system().map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
        Error::DbusConnection(err)
    })?;

    let conn_path = connection.with_proxy(
        "org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        DEFAULT_DBUS_TIMEOUT,
    );

    let guest_session_active = conn_path.is_guest_session_active().map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        Error::DbusGuestSessionActive(err)
    })?;

    Ok(guest_session_active)
}

fn root_dev() -> Result<String> {
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

/// Given a path to a block device with partition number, return its name.
///
/// For example: "/dev/nvme0n1p1" -> "nvme0n1"
fn blockdev_basename(mut dev: &str) -> Option<&str> {
    dev = dev.strip_prefix("/dev/")?;

    // The device name should be a single path component.
    if dev.contains('/') {
        return None;
    }

    // Remove partition number.
    if let Some((base, part)) = dev.rsplit_once('p') {
        // nvme0n1p3 -> nvme0n1
        if part.chars().all(|c: char| c.is_ascii_digit()) {
            return Some(base);
        }
    }

    // sda1 -> sda
    Some(dev.trim_end_matches(|c: char| c.is_ascii_digit()))
}

/// Print 'msg' followed by a [y/N] prompt and test the user input. Return true for 'y' or 'Y'.
pub fn prompt_for_yes(msg: &str) -> bool {
    print!("{} [y/N] ", msg);
    stdout().flush().ok();

    let mut response = String::new();
    stdin().read_line(&mut response).ok();
    matches!(response.as_str(), "y\n" | "Y\n")
}

// Creates a new epoll instance watching the provided fd.
pub fn add_epoll_for_fd(fd: i32) -> Result<i32> {
    // Safe because this will create a new epoll instance. Then we check the
    // return value.
    let epoll_fd = unsafe { libc::epoll_create1(0) };
    if epoll_fd < 0 {
        error!(
            "ERROR: Got unexpected result from epoll_create1(): {}",
            epoll_fd
        );
        return Err(Error::EpollCreate(epoll_fd));
    }

    let mut evt = epoll_event { events: 0, u64: 0 };
    // Safe because this only adds an entry to the epoll interest list. Then we
    // check the return value.
    let add_result = unsafe { libc::epoll_ctl(epoll_fd, libc::EPOLL_CTL_ADD, fd, &mut evt) };
    if add_result != 0 {
        error!("ERROR: Got unexpected epoll result: {}", add_result);
        return Err(Error::EpollAdd(add_result));
    }

    Ok(epoll_fd)
}

// Waits for an epoll event on the provided fd.
//
// The return value of the underlying syscall is returned.
pub fn epoll_wait(epoll_fd: i32) -> i32 {
    let mut evt = epoll_event { events: 0, u64: 0 };
    // Safe because we give an epoll_event array that can hold MAX_EPOLL_EVENTS.
    unsafe { libc::epoll_wait(epoll_fd, &mut evt, MAX_EPOLL_EVENTS, DEFAULT_EPOLL_TIMEOUT) }
}

#[cfg(test)]
mod tests {
    use regex::Regex;

    use super::*;

    #[test]
    fn test_generate_output_file_path() {
        // Set the user id hash env variable to a random value because it is necessary for output path generation.
        env::set_var(CROS_USER_ID_HASH, "useridhashfortesting");
        let expected_path_re = Regex::new(concat!(
            r"^/home/user/.+/MyFiles/Downloads/",
            r"packet_capture_\d{4}-\d{2}-\d{2}_\d{2}.\d{2}.\d{2}_.{6}\.pcap$"
        ))
        .unwrap();
        let result_output_path = generate_output_file_path("packet_capture", "pcap").unwrap();
        assert!(expected_path_re.is_match(&result_output_path));
    }

    #[test]
    fn blockdev_basename_valid() {
        assert_eq!(blockdev_basename("/dev/mmcblk0p3"), Some("mmcblk0"));
        assert_eq!(blockdev_basename("/dev/nvme0n1p1"), Some("nvme0n1"));
        assert_eq!(blockdev_basename("/dev/sda1"), Some("sda"));
    }

    #[test]
    fn blockdev_basename_invalid() {
        assert!(blockdev_basename("abcdef").is_none());
        assert!(blockdev_basename("/dev/abc/def").is_none());
    }
}
