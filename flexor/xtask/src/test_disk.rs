// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use std::path::Path;
use std::process::Command;

/// Runs a flexor test disk image.
pub fn run(flexor_disk: &Path) -> Result<()> {
    // Command copied from go/cros-frd/flexor.
    let mut cmd = Command::new("qemu-system-x86_64");
    cmd.args(["-enable-kvm", "-mem-prealloc", "-nographic"]);
    cmd.args(["-m", "16G"]);
    cmd.args(["-device", "qemu-xhci"]);
    cmd.args(["-device", "usb-tablet"]);
    cmd.args(["-rtc", "clock=host,base=localtime"]);
    cmd.args(["-display", "none"]);
    cmd.args(["-vga", "virtio"]);
    cmd.args(["-net", "user,hostfwd=tcp::10022-:22"]);
    cmd.args(["-net", "nic"]);
    cmd.args([
        "-chardev",
        "stdio,id=char0,mux=on,signal=on,logfile=flexor.log",
    ]);
    cmd.args(["-serial", "chardev:char0"]);
    cmd.args(["-mon", "chardev=char0"]);
    cmd.args(["-parallel", "none"]);
    cmd.args([
        "-drive",
        "if=pflash,format=raw,readonly=on,file=/usr/share/ovmf/OVMF.fd",
    ]);
    cmd.args([
        "-drive",
        &format!("format=raw,file={}", flexor_disk.display()),
    ]);

    println!("{cmd:?}");

    let status = cmd.status()?;

    if !status.success() {
        bail!("Qemu exited incorrectly.");
    }

    Ok(())
}
