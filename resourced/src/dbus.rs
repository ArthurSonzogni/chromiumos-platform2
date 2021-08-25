// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::raw::c_uint;
use std::os::unix::io::{AsRawFd, RawFd};
use std::time::Duration;

use anyhow::Result;
use dbus::ffidisp::{Connection, WatchEvent};
use dbus_tree::{Factory, MTFn, MethodErr, MethodInfo, MethodResult, Signal};
use sys_util::{error, PollContext, PollToken, TimerFd, WatchingEvents};

use crate::common;
use crate::memory;

const SERVICE_NAME: &str = "org.chromium.ResourceManager";
const PATH_NAME: &str = "/org/chromium/ResourceManager";
const INTERFACE_NAME: &str = SERVICE_NAME;

fn get_available_memory_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match memory::get_background_available_memory_kb() {
        // One message will be returned - the method return (and should always be there).
        Ok(available) => Ok(vec![m.msg.method_return().append1(available)]),
        Err(_) => Err(MethodErr::failed("Couldn't get available memory")),
    }
}

fn get_foreground_available_memory_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match memory::get_foreground_available_memory_kb() {
        Ok(available) => Ok(vec![m.msg.method_return().append1(available)]),
        Err(_) => Err(MethodErr::failed(
            "Couldn't get foreground available memory",
        )),
    }
}

#[allow(clippy::unnecessary_wraps)]
fn get_memory_margins_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    let margins = memory::get_memory_margins_kb();
    Ok(vec![m.msg.method_return().append2(margins.0, margins.1)])
}

fn get_game_mode(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match common::get_game_mode() {
        Ok(game_mode) => Ok(vec![m.msg.method_return().append1(game_mode as u8)]),
        Err(_) => Err(MethodErr::failed("Failed to get game mode")),
    }
}

fn set_game_mode(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    let mode = match m.msg.read1::<u8>()? {
        0 => common::GameMode::Off,
        1 => common::GameMode::Borealis,
        _ => return Err(MethodErr::failed("Unsupported game mode value")),
    };
    match common::set_game_mode(mode) {
        Ok(()) => Ok(vec![m.msg.method_return()]),
        Err(_) => Err(MethodErr::failed("Failed to set game mode")),
    }
}

fn get_rtc_audio_active(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match common::get_rtc_audio_active() {
        Ok(mode) => Ok(vec![m.msg.method_return().append1(mode as u8)]),
        Err(_) => Err(MethodErr::failed("Failed to get RTC audio activity")),
    }
}

fn set_rtc_audio_active(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    let mode = match m.msg.read1::<u8>()? {
        0 => common::RTCAudioActive::Inactive,
        1 => common::RTCAudioActive::Active,
        _ => return Err(MethodErr::failed("Unsupported RTC audio active value")),
    };
    match common::set_rtc_audio_active(mode) {
        Ok(()) => Ok(vec![m.msg.method_return()]),
        Err(_) => Err(MethodErr::failed("Failed to set RTC audio activity")),
    }
}

fn create_pressure_chrome_signal(f: &Factory<MTFn<()>, ()>) -> Signal<()> {
    f.signal("MemoryPressureChrome", ())
        .sarg::<u8, _>("pressure_level")
        .sarg::<u64, _>("reclaim_target_kb")
}

fn create_pressure_arcvm_signal(f: &Factory<MTFn<()>, ()>) -> Signal<()> {
    f.signal("MemoryPressureArcvm", ())
        .sarg::<u8, _>("pressure_level")
        .sarg::<u64, _>("reclaim_target_kb")
}

fn send_pressure_signal(conn: &Connection, signal: &Signal<()>, level: u8, reclaim_target_kb: u64) {
    let msg = signal
        .msg(&PATH_NAME.into(), &INTERFACE_NAME.into())
        .append2(level, reclaim_target_kb);
    if conn.send(msg).is_err() {
        error!("Send pressure signal failed.");
    }
}

pub fn service_main() -> Result<()> {
    // Starting up a connection to the system bus and request a name.
    let conn = Connection::new_system()?;
    conn.register_name(SERVICE_NAME, 0)?;

    let f = Factory::new_fn::<()>();

    // We create a tree with one object path inside and make that path introspectable.
    let tree = f.tree(()).add(
        f.object_path(PATH_NAME, ()).introspectable().add(
            f.interface(INTERFACE_NAME, ())
                .add_m(
                    f.method("GetAvailableMemoryKB", (), get_available_memory_kb)
                        // Our method has one output argument.
                        .outarg::<u64, _>("available"),
                )
                .add_m(
                    f.method(
                        "GetForegroundAvailableMemoryKB",
                        (),
                        get_foreground_available_memory_kb,
                    )
                    .outarg::<u64, _>("available"),
                )
                .add_m(
                    f.method("GetMemoryMarginsKB", (), get_memory_margins_kb)
                        .outarg::<u64, _>("reply"),
                )
                .add_m(
                    f.method("GetGameMode", (), get_game_mode)
                        .outarg::<u8, _>("game_mode"),
                )
                .add_m(
                    f.method("SetGameMode", (), set_game_mode)
                        .inarg::<u8, _>("game_mode"),
                )
                .add_s(create_pressure_chrome_signal(&f))
                .add_s(create_pressure_arcvm_signal(&f))
                .add_m(
                    f.method("GetRTCAudioActive", (), get_rtc_audio_active)
                        .outarg::<u8, _>("mode"),
                )
                .add_m(
                    f.method("SetRTCAudioActive", (), set_rtc_audio_active)
                        .inarg::<u8, _>("mode"),
                ),
        ),
    );

    tree.set_registered(&conn, true)?;
    conn.add_handler(tree);

    let pressure_chrome_signal = create_pressure_chrome_signal(&f);
    let pressure_arcvm_signal = create_pressure_arcvm_signal(&f);
    let check_timer = TimerFd::new()?;
    let check_interval = Duration::from_millis(1000);
    check_timer.reset(check_interval, Some(check_interval))?;

    #[derive(PollToken)]
    enum Token {
        TimerMsg,
        DBusMsg(RawFd),
    }

    let poll_ctx = PollContext::<Token>::new()?;

    poll_ctx.add(&check_timer.as_raw_fd(), Token::TimerMsg)?;
    for watch in conn.watch_fds() {
        let mut events = WatchingEvents::empty();
        if watch.readable() {
            events = events.set_read()
        }
        if watch.writable() {
            events = events.set_write()
        }
        poll_ctx.add_fd_with_events(&watch.fd(), events, Token::DBusMsg(watch.fd()))?;
    }

    loop {
        // Wait for events.
        for event in poll_ctx.wait()?.iter() {
            match event.token() {
                Token::TimerMsg => {
                    // wait() reads the fd. It's necessary to read periodic timerfd after each
                    // timerout.
                    check_timer.wait()?;
                    match memory::get_memory_pressure_status() {
                        Ok(pressure_status) => {
                            send_pressure_signal(
                                &conn,
                                &pressure_chrome_signal,
                                pressure_status.chrome_level as u8,
                                pressure_status.chrome_reclaim_target_kb,
                            );
                            if pressure_status.arcvm_level != memory::PressureLevelArcvm::None {
                                send_pressure_signal(
                                    &conn,
                                    &pressure_arcvm_signal,
                                    pressure_status.arcvm_level as u8,
                                    pressure_status.arcvm_reclaim_target_kb,
                                );
                            }
                        }
                        Err(e) => error!("get_memory_pressure_status() failed: {}", e),
                    }
                }
                Token::DBusMsg(fd) => {
                    let mut revents = 0;
                    if event.readable() {
                        revents += WatchEvent::Readable as c_uint;
                    }
                    if event.writable() {
                        revents += WatchEvent::Writable as c_uint;
                    }
                    // Iterate through the watch items would call next() to process messages.
                    for _item in conn.watch_handle(fd, revents) {}
                }
            }
        }
    }
}
