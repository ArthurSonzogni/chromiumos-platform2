// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a wrapper around chromeos-install.sh (formerly chromeos-install)
//! following the deshell playbook here:
//! https://github.com/google/deshell/blob/main/playbook.md

mod command_line;

use clap::Parser;
use nix::unistd;
use std::process::{Command, ExitCode};

use command_line::Args;

fn main() -> ExitCode {
    libchromeos::panic_handler::install_memfd_handler();

    // Fail if not running as root.
    if !unistd::Uid::effective().is_root() {
        eprintln!("chromeos-install must be run as root");
        return ExitCode::FAILURE;
    }

    let args = Args::parse();
    let mut install_cmd = Command::new("/usr/sbin/chromeos-install.sh");
    install_cmd.envs(args.to_env());

    println!("Running: {:?}", &install_cmd);

    if let Ok(status) = install_cmd.status() {
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
