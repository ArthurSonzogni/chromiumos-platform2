// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ctor::ctor;
use libchromeos::syslog;

pub mod common;

// mod handle_dlc_state_changed_test;
// mod handle_install_test;
// mod handle_prepare_shader_cache_test;
// mod handle_purge_test;
// mod handle_uninstall_test;
// mod handle_unmount_test;
// mod handle_vm_stopped_test;
// mod periodic_dlc_handler_test;

#[ctor]
fn global_init() {
    // One-time initializer for tests
    if let Err(e) = syslog::init_with_level(
        "shadercached_test".to_string(),
        true,
        syslog::LevelFilter::Debug,
    ) {
        panic!("Failed to initialize syslog: {}", e);
    }
}
