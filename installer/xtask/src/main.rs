// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
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

    /// Use the os_install_service (reven-only) to install, to test selinux.
    #[arg(long)]
    use_os_install_service: bool,

    /// Do a payload install using the passed image. Can be the same as test_image.
    /// The os_install_service doesn't support payload images, so these options
    /// are incompatible.
    #[arg(long, conflicts_with("os_install_service"))]
    payload_image: Option<PathBuf>,
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

fn crosvm_add_drive(cmd: &mut Command, id: &str, path: &str, readonly: bool) {
    let readonly = if readonly { "on" } else { "off" };

    // I think `cros vm` used to have a better interface for qemu args. Now you
    // need to pass each segment on its own, and args with dashes are tricky.
    cmd.arg("--qemu-args=-device");
    cmd.args(["--qemu-args", &format!("scsi-hd,drive={id}")]);
    cmd.arg("--qemu-args=-drive");
    cmd.args([
        "--qemu-args",
        &format!("if=none,id={id},format=raw,readonly={readonly},file={path}"),
    ]);
}

fn start_vm(installer_image: &Path, hdb: &Path, payload_image: &Option<PathBuf>) -> Result<()> {
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

    crosvm_add_drive(&mut cmd, "hdb", hdb_path, /* readonly */ false);

    // `cros vm` doesn't offer a way to copy files in, and scp would require
    // finding testing_rsa, etc. Load the payload image in as another drive.
    if let Some(payload_image) = payload_image {
        let payload_path = payload_image.to_str().unwrap();
        crosvm_add_drive(&mut cmd, "hdc", payload_path, /* readonly */ true);
    }

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

// Stages the payload image on the vm, and returns the path to it.
fn prep_payload_install() -> Result<String> {
    // Make a place to hold the payload image on the vm.
    let image_dir = "/tmp/image-dir";
    run_command(vm_command().args(["mkdir", &image_dir]))?;
    // Make a tmpfs big enough to hold the image, since there probably won't be
    // enough free space anywhere.
    run_command(vm_command().args(["mount", "-t", "tmpfs", "-o", "size=8G", "tmpfs", &image_dir]))?;

    let mut image_file = String::from(image_dir);
    image_file.push_str("/payload.bin");
    // Finally, copy the image into a file (we added it as a block device when
    // starting the vm).
    run_command(vm_command().args(["dd", "if=/dev/sdc", &format!("of={image_file}")]))?;

    Ok(image_file)
}

fn basic_install(payload_image: bool) -> Result<()> {
    println!("Running install...");

    let mut cmd = vm_command();
    cmd.args(["chromeos-install", "--dst", "/dev/sdb", "--yes"]);

    if payload_image {
        let payload_location = prep_payload_install()?;
        cmd.args(["--payload_image", &payload_location]);
    }

    run_command(&mut cmd).context("Couldn't install. Leaving vm running for debugging.")?;

    Ok(())
}

fn wait_for_os_install_service() -> Result<()> {
    println!("Waiting for install complete...");

    let mut monitor = vm_command()
        .args([
            "dbus-monitor",
            "--system",
            "sender=org.chromium.OsInstallService",
        ])
        .stdout(Stdio::piped())
        .spawn()?;

    let stdout = monitor.stdout.take().context("Couldn't get stdout")?;
    let reader = BufReader::new(stdout);
    let mut lines = reader
        .lines()
        .map_while(Result::ok)
        .inspect(|line| println!("{}", line));

    // Wait to see `member=OsInstallStatusChanged` (which signals install
    // ending) before proceeding.
    lines.find(|line| line.contains("member=OsInstallStatusChanged"));

    // We can stop the monitor, now.
    monitor.kill()?;

    // Print the next line, which says whether it's a success or not.
    let _ = lines.next();

    Ok(())
}

fn install_via_service() -> Result<()> {
    println!("Starting install via dbus...");

    let mut cmd = vm_command();

    cmd.args([
        "sudo",
        "-u",
        "chronos",
        "dbus-send",
        "--print-reply",
        "--system",
        "--dest=org.chromium.OsInstallService",
        "/org/chromium/OsInstallService",
        "org.chromium.OsInstallService.StartOsInstall",
    ]);

    run_command(&mut cmd).context("Couldn't trigger install. Leaving vm running for debugging.")?;

    // dbus-send doesn't block until complete, so let's wait on a signal that
    // indicates it's done.
    wait_for_os_install_service()?;

    Ok(())
}

fn run_test_install(args: &TestInstallArgs) -> Result<()> {
    let workdir = TempDir::new_in(".")?;
    let hdb = make_hdb(workdir.path())?;

    start_vm(&args.test_image, &hdb, &args.payload_image)?;

    if args.use_os_install_service {
        install_via_service()
    } else {
        basic_install(args.payload_image.is_some())
    }?;

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
