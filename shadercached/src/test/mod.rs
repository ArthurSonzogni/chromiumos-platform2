// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ctor::ctor;
use log::debug;

pub mod common;

mod handle_disk_space_update_test;
mod handle_dlc_state_changed_test;
mod handle_install_test;
mod handle_prepare_shader_cache_test;
mod handle_purge_test;
mod handle_uninstall_test;
mod handle_unmount_test;
mod handle_vm_stopped_test;
mod periodic_dlc_handler_test;

#[ctor]
fn global_init() {
    if stderrlog::new().verbosity(log::Level::Debug).init().is_ok() {
        debug!("Successfully initialized stderr logger for testing");
    }
}
