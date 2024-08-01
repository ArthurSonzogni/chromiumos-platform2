// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a wrapper around chromeos-install.sh (formerly chromeos-install)
//! following the deshell playbook here:
//! https://github.com/google/deshell/blob/main/playbook.md

use std::process::{Command, ExitCode};

fn main() -> ExitCode {
    // Don't include argv[0], the executable name, when passing args.
    let args = std::env::args().skip(1);

    if let Ok(status) = Command::new("/usr/sbin/chromeos-install.sh")
        .args(args)
        .status()
    {
        if status.success() {
            ExitCode::SUCCESS
        } else {
            ExitCode::FAILURE
        }
    } else {
        eprintln!("Couldn't launch chromeos-install.sh");
        ExitCode::FAILURE
    }
}
