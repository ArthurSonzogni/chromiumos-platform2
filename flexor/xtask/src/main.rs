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
    /// to test installation.
    RunTestDisk(RunTestDiskArgs),
}

#[derive(Args)]
struct RunTestDiskArgs {
    /// The disk image to test (e.g. flexor_disk.img).
    flexor_disk: PathBuf,
}

fn main() -> Result<()> {
    let opt = Opt::parse();

    match &opt.action {
        Action::RunTestDisk(args) => test_disk::run(&args.flexor_disk),
    }
}
