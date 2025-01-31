// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod file_view;
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
    /// to test installation, optionally updating some of the contained files before running.
    /// Ctrl+C to exit.
    RunTestDisk(TestDiskArgs),
    /// Takes a flexor image (like what the create_flexor_disk script produces) and updates some of
    /// the contained files.
    UpdateTestDisk(TestDiskArgs),
}

#[derive(Args)]
struct TestDiskArgs {
    /// The disk image for testing (e.g. flexor_disk.img).
    flexor_disk: PathBuf,

    /// Optionally update the flexor_vmlinuz.
    /// This contains the install script that will be run.
    #[arg(long)]
    flexor_vmlinuz: Option<PathBuf>,

    /// Optionally update the flex_image.tar.xz.
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
        Action::UpdateTestDisk(args) => {
            test_disk::update(&args.flexor_disk, &args.flexor_vmlinuz, &args.install_image)
        }
    }
}
