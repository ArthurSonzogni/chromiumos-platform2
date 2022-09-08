// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The dev module handles registration of crosh commands only enabled when ChromeOS is in developer
// mode.

mod shell;

use crate::dispatcher::Dispatcher;

pub fn register(dispatcher: &mut Dispatcher) {
    shell::register(dispatcher);
}
