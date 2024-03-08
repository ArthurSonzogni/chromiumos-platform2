// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;

fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();

    libchromeos::syslog::init("mini-udisks".to_string(), /*log_to_stderr=*/ true)
        .expect("failed to initialize logger");

    todo!()
}
