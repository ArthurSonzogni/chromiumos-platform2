// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use error::Result;
use libchromeos::panic_handler;

mod error;

fn main() -> Result<()> {
    // Setup the panic handler and logging.
    panic_handler::install_memfd_handler();
    println!("Hello from Flexor!");

    Ok(())
}
