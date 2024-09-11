// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a wrapper around chromeos-install.sh (formerly chromeos-install)
//! following the deshell playbook here:
//! https://github.com/google/deshell/blob/main/playbook.md

mod command_line;

use anyhow::{bail, Context, Result};
use clap::Parser;
use nix::unistd;
use std::process::Command;

use command_line::Args;

fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();

    // Fail if not running as root.
    if !unistd::Uid::effective().is_root() {
        bail!("chromeos-install must be run as root");
    }

    let args = Args::parse();
    let mut install_cmd = Command::new("/usr/sbin/chromeos-install.sh");
    install_cmd.envs(args.to_env());

    println!("Running: {:?}", &install_cmd);

    let status = install_cmd
        .status()
        .context("Couldn't launch chromeos-install.sh")?;

    if !status.success() {
        bail!("chromeos-install failed with code: {:?}", status.code());
    }

    Ok(())
}
