// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a wrapper around chromeos-install.sh (formerly chromeos-install)
//! following the deshell playbook here:
//! https://github.com/google/deshell/blob/main/playbook.md

mod command_line;
mod disk_util;
mod install_env;
mod install_source;
mod logger;
mod process_util;

use anyhow::{bail, Error, Result};
use clap::Parser;
use nix::unistd;
use std::process::Command;

use command_line::Args;
use install_source::InstallSource;
use process_util::log_and_run_command;

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
        if !payload_image.exists() {
            bail!("No payload image found at {}", payload_image.display());
        }
    }

    let source = InstallSource::from_args(args.skip_rootfs, args.payload_image.clone())?;

    let mut install_cmd = Command::new("/usr/sbin/chromeos-install.sh");
    // Convert most of our args into environment variables.
    install_cmd.envs(args.to_env());
    // The source supplies some env vars like SRC and ROOT.
    install_cmd.envs(source.to_env());
    // Adjust the environment based on what tools are available.
    install_cmd.envs(install_env::get_tool_env()?);

    log_and_run_command(install_cmd).map_err(Error::msg)
}
