// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::process;

use anyhow::bail;
use anyhow::Result;
use log::info;
use serde::Deserialize;
use std::process::Command;

/// Struct for deserializing the JSON output of `lsblk`.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq)]
pub struct LsBlkDevice {
    /// Device name.
    ///
    /// This is a full path because lsblk is run with "--paths".
    pub name: String,

    /// Device type.
    #[serde(rename = "type")]
    pub device_type: String,

    /// "Child" disks, e.g. partitions per disk.
    pub children: Option<Vec<LsBlkDevice>>,
}

#[derive(Debug, Deserialize, Eq, PartialEq)]
struct LsBlkOutput {
    #[serde(rename = "blockdevices")]
    block_devices: Vec<LsBlkDevice>,
}

/// Capture information about block devices from lsblk.
///
/// lsblk is a convenient tool that already exists on CrOS base builds
/// and in most other linux distributions. Using the "--json" flag
/// makes the output easily parsible.
///
/// Returns the raw output of lsblk.
fn get_lsblk_output() -> Result<Vec<u8>> {
    let mut command = process::Command::new("/bin/lsblk");
    command.args([
        // Select the fields to output
        "--output",
        "NAME,TYPE",
        // Format output as JSON
        "--json",
        // Print full device paths
        "--paths",
        // Exclude some devices by major number. See
        // https://www.kernel.org/doc/Documentation/admin-guide/devices.txt
        // for a list of major numbers.
        //
        // - Exclude floppy drives (2), as they are slow.
        // - Exclude scsi cdrom drives (11), as they are slow.
        // - Exclude zram (253), not a valid install target.
        "--exclude",
        "2,11,253",
    ]);

    get_command_output(command)
}

/// Capture information about block devices from lsblk.
///
/// Returns a flattened vector of devices.
pub fn get_lsblk_devices() -> Result<Vec<LsBlkDevice>> {
    let output = get_lsblk_output()?;
    let parsed: LsBlkOutput = serde_json::from_slice(&output)?;
    Ok(parsed.block_devices)
}

/// Run a command and get its stdout as raw bytes. An error is
/// returned if the process fails to launch, or if it exits non-zero.
fn get_command_output(mut command: Command) -> Result<Vec<u8>> {
    info!("running command: {:?}", command);
    let output = match command.output() {
        Ok(output) => output,
        Err(err) => {
            bail!("Failed to execute command: {err}");
        }
    };

    if !output.status.success() {
        bail!("Failed to execute command");
    }
    Ok(output.stdout)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mkdev(name: &str, dtype: &str, children: Option<Vec<LsBlkDevice>>) -> LsBlkDevice {
        LsBlkDevice {
            name: name.into(),
            device_type: dtype.into(),
            children,
        }
    }

    #[test]
    fn test_lsblk_deserialization() {
        // This test input was generated by running this command in a VM:
        //
        //     lsblk --bytes --output NAME,TYPE \
        //         --json --paths --exclude 2,11,253
        let input = include_bytes!("test_lsblk_output.json");

        #[rustfmt::skip]
        let expected = vec![
            mkdev("/dev/loop0", "loop", None),
            mkdev("/dev/loop1", "loop", Some(
                vec![mkdev("/dev/mapper/encstateful", "dm", None),]
            )),
            mkdev("/dev/loop2", "loop", None),
            mkdev("/dev/loop3", "loop", None),
            mkdev("/dev/loop4", "loop", None),
            mkdev("/dev/sda", "disk", Some(
                vec![
                    mkdev("/dev/sda1", "part", None),
                    mkdev("/dev/sda2", "part", None),
                    mkdev("/dev/sda3", "part", None),
                    mkdev("/dev/sda4", "part", None),
                    mkdev("/dev/sda5", "part", None),
                    mkdev("/dev/sda6", "part", None),
                    mkdev("/dev/sda7", "part", None),
                    mkdev("/dev/sda8", "part", None),
                    mkdev("/dev/sda9", "part", None),
                    mkdev("/dev/sda10", "part", None),
                    mkdev("/dev/sda11", "part", None),
                    mkdev("/dev/sda12", "part", None),
                ])
            ),
        ];

        let output: std::result::Result<LsBlkOutput, _> = serde_json::from_slice(input);
        assert!(output.is_ok());
        assert_eq!(output.unwrap().block_devices, expected);
    }
}
