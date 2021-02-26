// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::str::FromStr;
use std::{fmt, process};

use anyhow::Error;
use serde::de::{self, Visitor};
use serde::{Deserialize, Deserializer};

use crate::util::get_command_output;

/// Struct for deserializing the JSON output of `lsblk`.
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct LsBlkDevice {
    /// Device name.
    ///
    /// This is a full path because lsblk is run with "--paths".
    pub name: String,

    /// Internal kernel device name.
    #[serde(rename = "kname")]
    pub kernel_name: String,

    /// Whether the device is removable. This is not completely
    /// reliable, for example USB SSDs may not show as removable.
    #[serde(rename = "rm", deserialize_with = "deserialize_rm")]
    pub is_removable: bool,

    /// Size in bytes.
    #[serde(rename = "size", deserialize_with = "deserialize_size")]
    pub size_in_bytes: u64,

    /// Device type.
    #[serde(rename = "type")]
    pub device_type: String,
}

impl LsBlkDevice {
    /// Get the partition number, e.g. for `/dev/sda12` this returns 12.
    pub fn partition_number(&self) -> Option<u64> {
        if self.device_type != "part" {
            return None;
        }

        // Find the index of the last non-numeric character.
        let index = self.name.rfind(|c: char| !c.is_ascii_digit())?;

        // Parse the rest of the string past that index into a number.
        let num_part = &self.name[index + 1..];
        num_part.parse().ok()
    }
}

/// Deserialize the "rm" field of an lsblk device.
///
/// This is a boolean field, but the representation depends on the
/// version of lsblk. In 2.32 it uses "0" for false and "1" for
/// true, in 2.36.1 it uses proper true and false values.
fn deserialize_rm<'de, D>(deserializer: D) -> Result<bool, D::Error>
where
    D: Deserializer<'de>,
{
    struct StringOrBool;

    impl<'de> Visitor<'de> for StringOrBool {
        type Value = bool;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("string or bool")
        }

        fn visit_str<E>(self, value: &str) -> Result<bool, E>
        where
            E: de::Error,
        {
            match value {
                "0" => Ok(false),
                "1" => Ok(true),
                _ => Err(de::Error::custom("expected either '0' or '1'")),
            }
        }

        fn visit_bool<E>(self, value: bool) -> Result<bool, E>
        where
            E: de::Error,
        {
            Ok(value)
        }
    }

    deserializer.deserialize_any(StringOrBool)
}

/// Deserialize the "size" field of an lsblk device.
///
/// This is a numeric field, but the representation depends on the
/// version of lsblk. In 2.32 it wraps the number in a string, in
/// 2.36.1 it uses an actual number.
fn deserialize_size<'de, D>(deserializer: D) -> Result<u64, D::Error>
where
    D: Deserializer<'de>,
{
    struct StringOrU64;

    impl<'de> Visitor<'de> for StringOrU64 {
        type Value = u64;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("string or u64")
        }

        fn visit_str<E>(self, value: &str) -> Result<u64, E>
        where
            E: de::Error,
        {
            FromStr::from_str(value).map_err(|_| de::Error::custom("cannot parse string as u64"))
        }

        fn visit_u64<E>(self, value: u64) -> Result<u64, E>
        where
            E: de::Error,
        {
            Ok(value)
        }
    }

    deserializer.deserialize_any(StringOrU64)
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
struct LsBlkDeviceWithChildren {
    #[serde(flatten)]
    details: LsBlkDevice,

    /// Child devices.
    #[serde(default)]
    children: Vec<LsBlkDeviceWithChildren>,
}

#[derive(Debug, Deserialize, PartialEq)]
struct LsBlkOutput {
    #[serde(rename = "blockdevices")]
    block_devices: Vec<LsBlkDeviceWithChildren>,
}

impl LsBlkOutput {
    fn parse(input: &[u8]) -> Result<LsBlkOutput, serde_json::Error> {
        serde_json::from_slice(input)
    }

    fn flattened(self) -> Vec<LsBlkDevice> {
        let mut output = Vec::new();
        let mut stack = self.block_devices;
        while let Some(device) = stack.pop() {
            output.push(device.details);
            stack.extend(device.children);
        }
        output
    }
}

/// Capture information about block devices from lsblk.
///
/// lsblk is a convenient tool that already exists on CrOS base builds
/// and in most other linux distributions. Using the "--json" flag
/// makes the output easily parsible.
///
/// target: Block device to show information about. It will limit
/// lsblk to only return information about partitions on the target
/// device. If target is None lsblk will return information about most
/// block devices, excluding the zram device and slow devices such as
/// floppy drives.
///
/// Returns the raw output of lsblk.
fn get_lsblk_output(target_drive: Option<&Path>) -> Result<Vec<u8>, Error> {
    let mut command = process::Command::new("lsblk");
    command.args(&[
        // Print size in bytes
        "--bytes",
        // Select the fields to output
        "--output",
        "KNAME,NAME,RM,SIZE,TYPE",
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
    if let Some(target_drive) = target_drive {
        command.arg(target_drive);
    }
    Ok(get_command_output(command)?)
}

/// Capture information about block devices from lsblk.
///
/// target: Block device to show information about. It will limit
/// lsblk to only return information about partitions on the target
/// device. If target is None lsblk will return information about most
/// block devices, excluding the zram device and slow devices such as
/// floppy drives.
///
/// Returns a flattened vector of devices.
pub fn get_lsblk_devices(target_drive: Option<&Path>) -> Result<Vec<LsBlkDevice>, Error> {
    let output = get_lsblk_output(target_drive)?;
    let parsed = LsBlkOutput::parse(&output)?;
    Ok(parsed.flattened())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_partition_number() {
        let mkdev = |path: &str| LsBlkDevice {
            kernel_name: path.into(),
            name: path.into(),
            is_removable: false,
            size_in_bytes: 0,
            device_type: "part".into(),
        };

        // Valid partition devices.
        assert_eq!(mkdev("/dev/sda1").partition_number(), Some(1));
        assert_eq!(mkdev("/dev/sda12").partition_number(), Some(12));
        assert_eq!(mkdev("/dev/nvme0n1p3").partition_number(), Some(3));

        // Doesn't end in a number.
        assert_eq!(mkdev("/dev/dev").partition_number(), None);

        // Not a partition-type device.
        let mut dev = mkdev("/dev/sda1");
        dev.device_type = "disk".into();
        assert_eq!(dev.partition_number(), None);
    }

    fn mkdev(
        kname: &str,
        name: &str,
        is_removable: bool,
        size_in_bytes: u64,
        dtype: &str,
    ) -> LsBlkDevice {
        LsBlkDevice {
            kernel_name: kname.into(),
            name: name.into(),
            is_removable,
            size_in_bytes,
            device_type: dtype.into(),
        }
    }

    #[test]
    fn test_lsblk_deserialization() {
        // This test input was generated by running this command in a VM:
        //
        //     lsblk --bytes --output KNAME,NAME,RM,SIZE,TYPE \
        //         --json --paths --exclude 2,11,253
        let input = include_bytes!("test_lsblk_output.json");

        #[rustfmt::skip]
        let expected = vec![
            mkdev("/dev/sda", "/dev/sda", false, 6807435776, "disk"),
            mkdev("/dev/sda12", "/dev/sda12", false, 134217728, "part"),
            mkdev("/dev/sda11", "/dev/sda11", false, 8388608, "part"),
            mkdev("/dev/sda10", "/dev/sda10", false, 512, "part"),
            mkdev("/dev/sda9", "/dev/sda9", false, 512, "part"),
            mkdev("/dev/sda8", "/dev/sda8", false, 16777216, "part"),
            mkdev("/dev/sda7", "/dev/sda7", false, 512, "part"),
            mkdev("/dev/sda6", "/dev/sda6", false, 512, "part"),
            mkdev("/dev/sda5", "/dev/sda5", false, 2097152, "part"),
            mkdev("/dev/sda4", "/dev/sda4", false, 67108864, "part"),
            mkdev("/dev/sda3", "/dev/sda3", false, 2147483648, "part"),
            mkdev("/dev/sda2", "/dev/sda2", false, 67108864, "part"),
            mkdev("/dev/sda1", "/dev/sda1", false, 4295023104, "part"),
            mkdev("/dev/loop1", "/dev/loop1", false, 113876992, "loop"),
            mkdev("/dev/loop0", "/dev/loop0", false, 1248124928, "loop"),
            mkdev("/dev/dm-0", "/dev/mapper/encstateful", false, 1248124928, "dm"),
        ];

        let output = LsBlkOutput::parse(input).unwrap();
        assert_eq!(output.flattened(), expected);
    }

    fn mkdev2(
        kname: &str,
        name: &str,
        is_removable: bool,
        size_in_bytes: u64,
        dtype: &str,
    ) -> LsBlkDevice {
        LsBlkDevice {
            kernel_name: kname.into(),
            name: name.into(),
            is_removable,
            size_in_bytes,
            device_type: dtype.into(),
        }
    }

    #[test]
    fn test_lsblk_deserialization_v2_36_1() {
        // This test input was generated by running this command on a
        // machine with lsblk v2.36.1 (newer than what CrOS currently
        // has).
        //
        //     lsblk --bytes --output KNAME,NAME,RM,SIZE,TYPE \
        //         --json --paths --exclude 2,11,253
        let input = include_bytes!("test_lsblk_output_v2_36_1.json");

        #[rustfmt::skip]
        let expected = vec![
            mkdev2("/dev/nvme0n1", "/dev/nvme0n1", false, 1024209543168, "disk"),
            mkdev2("/dev/nvme0n1p3", "/dev/nvme0n1p3", false, 1020208477696, "part"),
            mkdev2("/dev/nvme0n1p2", "/dev/nvme0n1p2", false, 2000000000, "part"),
            mkdev2("/dev/nvme0n1p1", "/dev/nvme0n1p1", false, 2000000000, "part"),
            mkdev2("/dev/nvme1n1", "/dev/nvme1n1", false, 1000204886016, "disk"),
        ];

        let output = LsBlkOutput::parse(input).unwrap();
        assert_eq!(output.flattened(), expected);
    }

    #[test]
    fn test_lsblk_removable_deserialization() {
        // The "rm" field can be either a string or a bool depending
        // on the version of lsblk, verify that both work.
        let device: Vec<LsBlkDevice> = serde_json::from_value(serde_json::json!(
            [
                {"name":"a", "kname": "b", "rm":false, "size":"1", "type":"part"},
                {"name":"a", "kname": "b", "rm":true, "size":"1", "type":"part"},

                {"name":"a", "kname": "b", "rm":"0", "size":"1", "type":"part"},
                {"name":"a", "kname": "b", "rm":"1", "size":"1", "type":"part"}
            ]
        ))
        .unwrap();
        assert_eq!(
            device.iter().map(|d| d.is_removable).collect::<Vec<_>>(),
            vec![false, true, false, true]
        );
    }
}
