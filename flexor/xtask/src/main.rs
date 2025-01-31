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
#[expect(clippy::enum_variant_names)]
enum Action {
    /// Create a flexor test image.
    CreateTestDisk(CreateTestDisk),

    /// Takes a flexor image (like what the create-test-disk action produces) and runs it in a VM
    /// to test installation, optionally updating some of the contained files before running.
    /// Ctrl+C to exit.
    RunTestDisk(TestDiskArgs),

    /// Takes a flexor image (like what the create-test-disk action produces) and updates some of
    /// the contained files.
    UpdateTestDisk(TestDiskArgs),
}

#[derive(Args)]
struct CreateTestDisk {
    /// Path of the disk image to create.
    ///
    /// This will overwrite the file if it already exists.
    #[arg(long, default_value = "flexor_disk.img")]
    output: PathBuf,

    /// Path of an unpacked FRD bundle from go/cros-frd-releases.
    #[arg(long)]
    frd_bundle: PathBuf,
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
        Action::CreateTestDisk(args) => test_disk::create(args),
        Action::RunTestDisk(args) => {
            test_disk::update(args)?;
            test_disk::run(&args.flexor_disk)
        }
        Action::UpdateTestDisk(args) => test_disk::update(args),
    }
}
