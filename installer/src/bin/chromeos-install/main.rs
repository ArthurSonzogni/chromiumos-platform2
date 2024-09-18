// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a wrapper around chromeos-install.sh (formerly chromeos-install)
//! following the deshell playbook here:
//! https://github.com/google/deshell/blob/main/playbook.md

mod command_line;
mod logger;

use anyhow::{bail, Context, Result};
use clap::Parser;
use log::{debug, info};
use nix::unistd;
use std::path::Path;
use std::process::Command;

use command_line::Args;

fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();

    let args = Args::parse();

    logger::init(args.debug)?;

    // Fail if not running as root.
    if !unistd::Uid::effective().is_root() {
        bail!("chromeos-install must be run as root");
    }

    // If using a payload image, make sure it exists.
    if let Some(payload_image) = &args.payload_image {
        if !Path::new(&payload_image).exists() {
            bail!("No payload image found at {}", payload_image);
        }
    }

    let mut install_cmd = Command::new("/usr/sbin/chromeos-install.sh");
    install_cmd.envs(args.to_env());

    info!("Running: {:?}", &install_cmd);
    debug!("with env: {:?}", install_cmd.get_envs());

    let status = install_cmd
        .status()
        .context("Couldn't launch chromeos-install.sh")?;

    if !status.success() {
        bail!("chromeos-install failed with code: {:?}", status.code());
    }

    Ok(())
}
