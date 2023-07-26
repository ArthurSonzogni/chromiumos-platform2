// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "hibernate" for crosh which tries to hibernate the
// system (if hibernate is available and enabled).

use std::fs;
use std::io::Write;
use std::path::Path;
use std::thread;
use std::time;

use dbus::blocking::Connection;
use getopts::{Matches, Options};
use log::error;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::board_name;
use crate::util::is_chrome_feature_enabled;
use crate::util::is_guest_session_active;
use crate::util::DEFAULT_DBUS_TIMEOUT;

const SUSPEND_FLAVOR_HIBERNATE: u32 = 2;

// short name, long name, description, hint
const FLAGS: [(&str, &str, &str, &str); 1] = [(
    "n",
    "no-delay",
    "skip the delay before initiating the hibernation.",
    "",
)];

#[derive(Debug, Clone)]
struct InvalidArgumentError;

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "hibernate".to_string(),
            "".to_string(),
            r#"Attempt to hibernate the system. Only available on devices for which
  hibernate is enabled."#
                .to_string(),
        )
        .set_command_callback(Some(hibernate))
        .set_help_callback(hibernate_help),
    );
}

fn hibernate(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let matches = parse_options(args).map_err(|_err| {
        println!("{}", hibernate_usage());
        dispatcher::Error::CommandReturnedError
    })?;

    let delay = if matches.opt_present("n") {
        None
    } else {
        Some(time::Duration::from_secs(2))
    };

    if !board_supports_hibernate() {
        println!("  hibernate is not available for {}", board_name());
        return Ok(());
    }

    let guest_session_active = is_guest_session_active().map_err(|err| {
        error!("ERROR: Failed to check if a guest session is active: {err:?}");
        dispatcher::Error::CommandReturnedError
    })?;

    if guest_session_active {
        println!("  hibernate is not available in guest mode");
        return Ok(());
    }

    let hibernate_disabled = !is_chrome_feature_enabled("IsSuspendToDiskEnabled").map_err(|e| {
        error!("ERROR: Failed to check if Chrome feature is enabled: {}", e);
        dispatcher::Error::CommandReturnedError
    })?;

    if hibernate_disabled {
        println!("  hibernate is supported but disabled. It can be enabled through");
        println!(r#"  chrome://flags ("Enable Suspend to Disk")."#);
        return Ok(());
    }

    if !hiberimage_exists() {
        println!("  hiberimage does not exist");
        return Ok(());
    }

    request_hibernate(delay)
}

fn request_hibernate(delay: Option<time::Duration>) -> Result<(), dispatcher::Error> {
    let connection = Connection::new_system().map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;

    // The abortion of hibernate as a result of user input is a byproduct of
    // how suspend works on Chrome OS. When a suspend request is received
    // powerd records the current number of wakeup events. It then prepares
    // the system for suspend. The wakeup count is checked again before
    // actually suspending the system, if it has increased the suspend
    // attempt is aborted.
    println!(
        r#"  System will now hibernate. Please do not press any keys or use the
  touchpad/mouse until the system has fully shutdown, otherwise the
  system might abort hibernate."#
    );

    if let Some(delay) = delay {
        thread::sleep(delay);
    }

    let proxy = connection.with_proxy(
        "org.chromium.PowerManager",
        "/org/chromium/PowerManager",
        DEFAULT_DBUS_TIMEOUT,
    );

    proxy
        .method_call(
            "org.chromium.PowerManager",
            "RequestSuspend",
            (SUSPEND_FLAVOR_HIBERNATE,),
        )
        .map_err(|err| {
            error!("ERROR: D-Bus method call failed: {}", err);
            dispatcher::Error::CommandReturnedError
        })?;

    Ok(())
}

fn parse_options(args: &Arguments) -> Result<Matches, InvalidArgumentError> {
    let opts = get_options();

    let matches = opts
        .parse(args.get_tokens())
        .map_err(|_err_| InvalidArgumentError)?;

    if matches.free.len() != 1 {
        return Err(InvalidArgumentError);
    }

    Ok(matches)
}

fn get_options() -> Options {
    let mut opts = Options::new();

    for flag in FLAGS.iter() {
        if flag.3.is_empty() {
            opts.optflag(flag.0, flag.1, flag.2);
        } else {
            opts.optopt(flag.0, flag.1, flag.2, flag.3);
        }
    }

    opts
}

fn hibernate_usage() -> String {
    let opts = get_options();
    opts.usage("Usage: hibernate [options]")
}

fn hibernate_help(_cmd: &Command, w: &mut dyn Write, _level: usize) {
    let usage = hibernate_usage();
    w.write_all(usage.as_bytes()).unwrap();
    w.flush().unwrap();
}

fn board_supports_hibernate() -> bool {
    Path::new("/usr/sbin/hiberman").exists()
}

fn hiberimage_exists() -> bool {
    let block_dir = Path::new("/sys/class/block");

    for entry in fs::read_dir(block_dir).unwrap().flatten() {
        let bdev_name = entry.file_name().to_string_lossy().to_string();
        if !bdev_name.starts_with("dm-") {
            continue;
        }

        let dm_name_attr = block_dir.join(bdev_name).join("dm").join("name");
        let res = fs::read_to_string(dm_name_attr);
        if let Ok(dm_name) = res {
            if dm_name.trim() == "hiberimage" {
                return true;
            }
        }
    }

    false
}
