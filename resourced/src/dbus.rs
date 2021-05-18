// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::thread;
use std::time::Duration;

use anyhow::Result;
use dbus::blocking::LocalConnection;
use dbus::channel::Sender; // For LocalConnection::send()
use dbus::tree::{Factory, MTFn, MethodErr, MethodInfo, MethodResult, Signal};
use sys_util::error;

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

pub struct ServiceContext {
    connection: LocalConnection,
}

fn create_pressure_chrome_signal(f: &Factory<MTFn<()>, ()>) -> Signal<()> {
    f.signal("MemoryPressureChrome", ())
        .sarg::<u8, _>("pressure_level")
        .sarg::<u64, _>("memory_delta")
}

pub fn service_init() -> Result<ServiceContext> {
    // Starting up a connection to the system bus and request a name.
    let conn = LocalConnection::new_system()?;
    conn.request_name(SERVICE_NAME, false, true, false)?;

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
                .add_s(create_pressure_chrome_signal(&f)),
        ),
    );

    tree.start_receive(&conn);

    Ok(ServiceContext { connection: conn })
}

pub fn service_main_loop(context: ServiceContext) -> Result<()> {
    // Serve clients forever.
    loop {
        context
            .connection
            .process(Duration::from_millis(u64::MAX))?;
    }
}

fn send_pressure_chrome_signal(conn: &LocalConnection, signal: &Signal<()>, level: u8, delta: u64) {
    if conn
        .send(
            signal
                .msg(&PATH_NAME.into(), &INTERFACE_NAME.into())
                .append2(level, delta),
        )
        .is_err()
    {
        error!("Send pressure chrome signal failed.");
    }
}

pub fn check_memory_main() -> Result<()> {
    let signal = create_pressure_chrome_signal(&Factory::new_fn::<()>());
    let conn = LocalConnection::new_system()?;
    loop {
        // TODO(vovoy): Reduce signal frequency when memory pressure is low.
        match memory::get_memory_pressure_status_chrome() {
            Ok((level, delta)) => send_pressure_chrome_signal(&conn, &signal, level as u8, delta),
            Err(e) => error!("Couldn't get memory pressure status for chrome: {}", e),
        }
        thread::sleep(Duration::from_secs(1));
    }
}
