// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod dbus;
mod memory;

#[cfg(test)]
mod test;

use anyhow::Result;

fn main() -> Result<()> {
    dbus::start_service()
}
