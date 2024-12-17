// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_disk;

use anyhow::Result;
use clap::{Args, Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
struct Opt {
    #[command(subcommand)]
    action: Action,
}

#[derive(Subcommand)]
enum Action {
    /// Takes a flexor image (like what the create_flexor_disk script produces) and runs it in a VM
    /// to test installation, optionally updating some of the contained files.
    /// Ctrl+C to exit.
    RunTestDisk(RunTestDiskArgs),
}

#[derive(Args)]
struct RunTestDiskArgs {
    /// The disk image to test (e.g. flexor_disk.img).
    flexor_disk: PathBuf,

    /// Optionally update the flexor_vmlinuz before running.
    /// This contains the install script that will be run.
    #[arg(long)]
    flexor_vmlinuz: Option<PathBuf>,

    /// Optionally update the flex_image.tar.xz before running.
    /// This is the image that will be installed by flexor.
    // TODO(tbrandston): Optional improvement: detect non-xz input and use tar+flate2 to compress.
    #[arg(long)]
    install_image: Option<PathBuf>,
}

fn main() -> Result<()> {
    let opt = Opt::parse();

    match &opt.action {
        Action::RunTestDisk(args) => {
            test_disk::update(&args.flexor_disk, &args.flexor_vmlinuz, &args.install_image)?;
            test_disk::run(&args.flexor_disk)
        }
    }
}
