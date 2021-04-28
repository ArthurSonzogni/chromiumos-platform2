// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use anyhow::Result;
use dbus::blocking::LocalConnection;
use dbus::tree::{Factory, MTFn, MethodErr, MethodInfo, MethodResult};

use crate::common;
use crate::memory;

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
        Ok(common::GameMode::Off) => Ok(vec![m.msg.method_return().append1(0u8)]),
        Ok(common::GameMode::Borealis) => Ok(vec![m.msg.method_return().append1(1u8)]),
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

pub fn start_service() -> Result<()> {
    let service_name = "org.chromium.ResourceManager";
    let path_name = "/org/chromium/ResourceManager";
    let interface_name = service_name;
    // Let's start by starting up a connection to the system bus and request a name.
    let c = LocalConnection::new_system()?;
    c.request_name(service_name, false, true, false)?;

    let f = Factory::new_fn::<()>();

    // We create a tree with one object path inside and make that path introspectable.
    let tree = f.tree(()).add(
        f.object_path(path_name, ()).introspectable().add(
            f.interface(interface_name, ())
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
                ),
        ),
    );

    tree.start_receive(&c);

    // Serve clients forever.
    loop {
        c.process(Duration::from_millis(u64::MAX))?;
    }
}
