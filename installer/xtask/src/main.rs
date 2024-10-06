// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[derive(Parser)]
struct Opt {
    #[command(subcommand)]
    action: Action,
}

#[derive(Subcommand)]
enum Action {
    /// Given a test image, runs an install in a vm to test basic functionality.
    TestInstall(TestInstallArgs),
}

#[derive(Args)]
struct TestInstallArgs {
    /// Image to install from.
    test_image: PathBuf,
}

// Make running commands a little nicer:
// * Log the command being run
// * Handle the two-level status check
fn run_command(cmd: &mut Command) -> Result<()> {
    let cmd_str = format!("{:?}", cmd).replace('"', "");
    println!("Running: {cmd_str}");

    let status = cmd.status()?;
    if !status.success() {
        bail!("Command failed with: {:?}", status.code());
    }

    Ok(())
}

fn make_hdb(in_dir: &Path) -> Result<PathBuf> {
    let mut path = in_dir.to_path_buf();
    path.push("hdb");

    let mut cmd = Command::new("qemu-img");
    cmd.args(["create", "-f", "raw"]);
    cmd.arg(&path);
    cmd.arg("16G");

    run_command(&mut cmd).with_context(|| format!("Couldn't create hdb at {}", path.display()))?;

    Ok(path)
}

fn cros_vm() -> Command {
    let mut cmd = Command::new("cros");
    cmd.arg("vm");

    cmd
}

fn start_vm(installer_image: &Path, hdb: &Path) -> Result<()> {
    println!("Setting up vm...");

    let image_path = installer_image.to_str().unwrap();
    let hdb_path = hdb.to_str().unwrap();
    let mut cmd = cros_vm();
    cmd.arg("--start");

    // Don't mutate the installer image.
    cmd.arg("--copy-on-write");
    // `cros vm` wants a board argument, but doesn't actually use it
    // when `--image-path` is passed, or any case we run into here.
    cmd.args(["--board", "ignored"]);

    cmd.args(["--image-path", image_path]);

    // I think `cros vm` used to have a better interface for qemu args. Now you
    // need to pass each segment on its own, and args with dashes are tricky.
    cmd.arg("--qemu-args=-device");
    cmd.args(["--qemu-args", "scsi-hd,drive=hdb"]);
    cmd.arg("--qemu-args=-drive");
    cmd.args([
        "--qemu-args",
        &format!("if=none,id=hdb,format=raw,file={hdb_path}"),
    ]);

    run_command(&mut cmd).context("Couldn't start vm")?;

    Ok(())
}

fn stop_vm() -> Result<()> {
    println!("Shutting down vm...");

    let mut cmd = cros_vm();
    cmd.arg("--stop");

    run_command(&mut cmd).context("Couldn't stop vm, so it may still be running.")?;

    Ok(())
}

fn vm_command() -> Command {
    let mut cmd = cros_vm();

    cmd.args(["--cmd", "--"]);

    cmd
}

fn basic_install() -> Result<()> {
    println!("Running install...");

    let mut cmd = vm_command();
    cmd.args(["chromeos-install", "--dst", "/dev/sdb", "--yes"]);

    run_command(&mut cmd).context("Couldn't install. Leaving vm running for debugging.")?;

    Ok(())
}

fn run_test_install(args: &TestInstallArgs) -> Result<()> {
    let workdir = TempDir::new_in(".")?;
    let hdb = make_hdb(workdir.path())?;

    start_vm(&args.test_image, &hdb)?;

    basic_install()?;

    stop_vm()?;

    Ok(())
}

fn main() -> Result<()> {
    let opt = Opt::parse();

    match &opt.action {
        Action::TestInstall(args) => run_test_install(args),
    }?;

    Ok(())
}