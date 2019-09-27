// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The base module handles registration of a base set of crosh commands.

mod set_time;
mod vmc;

use crate::dispatcher::Dispatcher;

pub fn register(dispatcher: &mut Dispatcher) {
    set_time::register(dispatcher);
    vmc::register(dispatcher);
}
