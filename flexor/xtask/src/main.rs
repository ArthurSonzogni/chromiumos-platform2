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
    /// Create a flexor test image that will boot and install Flex.
    CreateTestDisk(CreateArgs),

    /// Run a flexor image (like what create-test-disk produces) in a VM to test installation.
    /// Ctrl+C or quit vm to exit.
    RunTestDisk(RunArgs),
}

#[derive(Args)]
struct CreateArgs {
    /// Path of the disk image to create.
    ///
    /// This will overwrite the file if it already exists.
    #[arg(long, default_value = "flexor_disk.img")]
    output: PathBuf,

    /// Path of an unpacked FRD bundle from go/cros-frd-releases.
    #[arg(long)]
    frd_bundle: PathBuf,

    /// Use a different flexor. flexor_vmlinuz contains the flexor binary, install script, etc.
    #[arg(long)]
    flexor_vmlinuz: Option<PathBuf>,

    /// Install a different flex image (must be a flex_image.tar.xz).
    // TODO(tbrandston): Optional improvement: detect non-xz input and use tar+flate2 to compress.
    #[arg(long)]
    install_image: Option<PathBuf>,

    /// Use a different crdyshim.
    #[arg(long)]
    crdyshim: Option<PathBuf>,

    /// Use a different crdyboot (automatically grabs .sig file, too).
    #[arg(long)]
    crdyboot: Option<PathBuf>,

    /// Turn on crdyboot_verbose logging (also makes flexor log visibly).
    #[arg(long)]
    crdyboot_verbose: bool,
}

#[derive(Args)]
struct RunArgs {
    /// The disk image for testing (e.g. flexor_disk.img).
    flexor_disk: PathBuf,
}

fn main() -> Result<()> {
    let opt = Opt::parse();

    match &opt.action {
        Action::CreateTestDisk(args) => test_disk::create(args),
        Action::RunTestDisk(args) => test_disk::run(&args.flexor_disk),
    }
}
