// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The base module handles registration of a base set of crosh commands.

mod arc;
mod bt_console;
mod ccd_pass;
mod connectivity;
mod display_debug;
mod dlc_install;
mod dlc_list;
mod dmesg;
mod evtest;
mod exit;
mod force_fips;
mod free;
mod generate_firmware_dump;
mod help_advanced;
mod insert_coin;
mod meminfo;
mod packet_capture;
mod printscan_debug;
mod rollback;
mod set_time;
mod sync;
mod syslog;
mod top;
mod u2f_flags;
mod uname;
mod upload_crashes;
mod upload_devcoredumps;
mod uptime;
mod verify_ro;
mod vmc;
mod vmstat;
mod wireguard;

use crate::dispatcher::Dispatcher;

pub fn register(dispatcher: &mut Dispatcher) {
    arc::register(dispatcher);
    bt_console::register(dispatcher);
    ccd_pass::register(dispatcher);
    connectivity::register(dispatcher);
    dlc_install::register(dispatcher);
    dlc_list::register(dispatcher);
    display_debug::register(dispatcher);
    dmesg::register(dispatcher);
    evtest::register(dispatcher);
    exit::register(dispatcher);
    force_fips::register(dispatcher);
    free::register(dispatcher);
    generate_firmware_dump::register(dispatcher);
    help_advanced::register(dispatcher);
    insert_coin::register(dispatcher);
    meminfo::register(dispatcher);
    packet_capture::register(dispatcher);
    printscan_debug::register(dispatcher);
    rollback::register(dispatcher);
    set_time::register(dispatcher);
    sync::register(dispatcher);
    syslog::register(dispatcher);
    top::register(dispatcher);
    u2f_flags::register(dispatcher);
    upload_crashes::register(dispatcher);
    upload_devcoredumps::register(dispatcher);
    uptime::register(dispatcher);
    uname::register(dispatcher);
    verify_ro::register(dispatcher);
    vmc::register(dispatcher);
    vmstat::register(dispatcher);
    wireguard::register(dispatcher);
}
