// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command line argument parsing, with a focus on matching the old shell
//! script's arguments.

use clap::Parser;
use libinstall::process_util::Environment;
use std::ffi::OsString;
use std::path::PathBuf;

/// Arg parser with the set of args from the shell script.
///
/// This drops the '--no<flag>' variant of each boolean arg,
/// because they didn't seem to be used and would have been
/// complex to add.
#[derive(Parser, Debug)]
#[command(version, about, rename_all = "snake_case")]
pub struct Args {
    /// Destination device
    #[arg(long)]
    pub dst: Option<PathBuf>,

    /// Skip check to ensure destination is not removable
    #[arg(long)]
    pub skip_dst_removable: bool,

    /// Answer yes to everything
    #[arg(short, long)]
    pub yes: bool,

    /// Skip installing the rootfs; Only set up partition table and clear and
    /// reinstall the stateful partition.
    // TODO(b/365748385): find a better name for this, since it doesn't just
    // skip ROOT-A/B, but also KERN-* and all the others.
    #[arg(long, requires("pmbr_code"))]
    pub skip_rootfs: bool,

    /// Don't create a new filesystem for the stateful partition. Be careful
    /// using this as this may make the stateful partition not mountable.
    #[arg(long)]
    pub preserve_stateful: bool,

    /// Path to a Chromium OS image to install onto the device's hard drive
    // TODO(b/356344778): Does this _have_ to conflict with skip_rootfs?
    // See `check_payload_image` in chromeos-install.sh to understand why.
    // I expect this to get worked out as we continue to rustify.
    #[arg(long, conflicts_with("skip_rootfs"))]
    pub payload_image: Option<PathBuf>,

    /// Path to PMBR code to be installed
    #[arg(long)]
    pub pmbr_code: Option<PathBuf>,

    /// Bios type to boot with (see postinst --bios)
    #[arg(long)]
    pub target_bios: Option<String>,

    /// Show debug output
    #[arg(long)]
    pub debug: bool,

    /// Print storage diagnostic information on failure
    #[arg(long)]
    pub storage_diags: bool,

    /// Skips creating the GPT partition table.
    #[arg(long)]
    pub skip_gpt_creation: bool,

    /// Path to a file containing logs to be preserved
    #[arg(long)]
    pub lab_preserve_logs: Option<PathBuf>,

    /// Skip postinstall for situations where you're building for a non-native
    /// arch. Note that this will probably break verity.
    #[arg(long)]
    pub skip_postinstall: bool,

    /// Minimal copy of partitions.
    #[arg(long)]
    pub minimal_copy: bool,
}

impl Args {
    /// Convert parsed args into "environment variables" (pairs of Strings) to be
    /// passed to the shell script.
    pub fn to_env(&self) -> Environment {
        let mut output = Environment::new();

        // Convert from `bool` to `OsString`, using the shflags format of "0"/"1"
        // for true and false.
        fn sh_bool(value: bool) -> OsString {
            if value { "0" } else { "1" }.into()
        }

        // Convert from `Option<PathBuf>` to `OsString`, with `None` producing an empty string.
        fn sh_path(value: &Option<PathBuf>) -> OsString {
            value.clone().unwrap_or_default().into()
        }

        output.extend([
            ("FLAGS_skip_dst_removable", sh_bool(self.skip_dst_removable)),
            ("FLAGS_skip_rootfs", sh_bool(self.skip_rootfs)),
            ("FLAGS_yes", sh_bool(self.yes)),
            ("FLAGS_preserve_stateful", sh_bool(self.preserve_stateful)),
            ("FLAGS_debug", sh_bool(self.debug)),
            ("FLAGS_skip_postinstall", sh_bool(self.skip_postinstall)),
            ("FLAGS_storage_diags", sh_bool(self.storage_diags)),
            ("FLAGS_minimal_copy", sh_bool(self.minimal_copy)),
            ("FLAGS_skip_gpt_creation", sh_bool(self.skip_gpt_creation)),
        ]);

        output.extend([
            ("FLAGS_dst", sh_path(&self.dst)),
            ("FLAGS_payload_image", sh_path(&self.payload_image)),
            ("FLAGS_pmbr_code", sh_path(&self.pmbr_code)),
            ("FLAGS_lab_preserve_logs", sh_path(&self.lab_preserve_logs)),
        ]);

        // --target-bios is a String, so can't go in the loop with the PathBufs.
        output.insert(
            "FLAGS_target_bios",
            self.target_bios.clone().unwrap_or_default().into(),
        );

        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::ffi::OsString;

    #[test]
    fn test_args_to_env() {
        let args = Args::parse_from(["arg0", "--yes"]);
        let env = args.to_env();

        // Boolean flags come through with the right format.
        assert!(env
            .iter()
            .any(|(key, val)| *key == "FLAGS_yes" && val == "0"));

        // Boolean flags have the right default.
        assert!(env
            .iter()
            .any(|(key, val)| *key == "FLAGS_debug" && val == "1"));

        // String flags are empty if not specified.
        let target_bios = env.iter().find(|(key, _)| **key == "FLAGS_target_bios");
        assert_eq!(*target_bios.unwrap().1, OsString::new());
    }

    #[test]
    fn skip_rootfs_needs_pmbr() {
        let args = Args::try_parse_from(["arg0", "--skip_rootfs"]);
        assert!(args.is_err());
    }

    #[test]
    fn payload_image_cant_skip_rootfs() {
        let args = Args::try_parse_from(["arg0", "--skip_rootfs", "--payload_image", "/path"]);
        assert!(args.is_err());
    }
}
