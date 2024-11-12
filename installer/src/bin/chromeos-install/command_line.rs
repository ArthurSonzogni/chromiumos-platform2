// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command line argument parsing, with a focus on matching the old shell
//! script's arguments.

use crate::process_util::Environment;
use clap::Parser;
use std::ffi::OsString;

// To allow the default use of lvm to be controlled by USE flag, toggle this
// bool based on the feature `lvm_default`.
#[cfg(feature = "lvm_stateful_partition")]
const LVM_FLAG_DEFAULT: bool = true;

#[cfg(not(feature = "lvm_stateful_partition"))]
const LVM_FLAG_DEFAULT: bool = false;

#[cfg(feature = "default_key_stateful")]
const DEFAULT_KEY_FLAG_DEFAULT: bool = true;

#[cfg(not(feature = "default_key_stateful"))]
const DEFAULT_KEY_FLAG_DEFAULT: bool = false;

/// Arg parser with the set of args from the shell script.
///
/// This drops the '--no<flag>' variant of each boolean arg (except for
/// lvm_stateful), because they didn't seem to be used and would have been
/// complex to add.
#[derive(Parser, Debug)]
#[command(version, about, rename_all = "snake_case")]
pub struct Args {
    /// Destination device
    #[arg(long)]
    pub dst: Option<String>,

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
    pub payload_image: Option<String>,

    /// Path to PMBR code to be installed
    #[arg(long)]
    pub pmbr_code: Option<String>,

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

    /// Create LVM-based stateful partition
    #[arg(long)]
    pub lvm_stateful: bool,

    /// Don't create LVM-based stateful partition
    #[arg(long, conflicts_with("lvm_stateful"))]
    pub nolvm_stateful: bool,

    /// Create dm-default-key encrypted stateful metadata partition
    #[arg(long, conflicts_with("lvm_stateful"))]
    pub default_key_stateful: bool,

    /// Don't dm-default-key encrypted stateful metadata partition
    #[arg(long, conflicts_with("default_key_stateful"))]
    pub nodefault_key_stateful: bool,

    /// Path to a file containing logs to be preserved
    #[arg(long)]
    pub lab_preserve_logs: Option<String>,

    /// Skip postinstall for situations where you're building for a non-native
    /// arch. Note that this will probably break verity.
    #[arg(long)]
    pub skip_postinstall: bool,

    /// Minimal copy of partitions.
    #[arg(long)]
    pub minimal_copy: bool,
}

impl Args {
    /// The default for the lvm_stateful flag has historically been controlled via
    /// USE flag. In the case where the default was set to "true" it could be turned
    /// off with the `--nolvm_stateful` flag. We keep that functionality, and figure
    /// out the correct value to pass to the script by assuming that flags are only
    /// specified when trying to change the default.
    /// We can probably do this with clever Clap settings, but this seems simpler.
    fn lvm_stateful_arg(&self, default: bool) -> bool {
        // A `true` value for a flag indicates that it was passed, a `false` means
        // it wasn't. When the default is to do lvm, we can ignore the 'positive'
        // lvm flag and when the default is to not we can ignore the negative.
        // If a user passes both positive and negative, Clap should abort.
        if default {
            !self.nolvm_stateful
        } else {
            self.lvm_stateful
        }
    }

    fn default_key_stateful_arg(&self, default: bool) -> bool {
        if default {
            !self.nodefault_key_stateful
        } else {
            self.default_key_stateful
        }
    }

    /// Convert parsed args into "environment variables" (pairs of Strings) to be
    /// passed to the shell script.
    pub fn to_env(&self) -> Environment {
        let mut output = Environment::new();

        // Convert from `bool` to `OsString`, using the shflags format of "0"/"1"
        // for true and false.
        fn sh_bool(value: bool) -> OsString {
            if value { "0" } else { "1" }.into()
        }

        // Convert from `Option<String>` to `OsString`, with `None` producing an empty string.
        fn sh_path(value: &Option<String>) -> OsString {
            value.clone().unwrap_or_default().into()
        }

        // Special handling for flag with USE-flag controlled default.
        let lvm_stateful = self.lvm_stateful_arg(LVM_FLAG_DEFAULT);
        let default_key_stateful = self.default_key_stateful_arg(DEFAULT_KEY_FLAG_DEFAULT);

        // Boolean flags
        output.extend([
            ("FLAGS_skip_dst_removable", sh_bool(self.skip_dst_removable)),
            ("FLAGS_skip_rootfs", sh_bool(self.skip_rootfs)),
            ("FLAGS_yes", sh_bool(self.yes)),
            ("FLAGS_preserve_stateful", sh_bool(self.preserve_stateful)),
            ("FLAGS_debug", sh_bool(self.debug)),
            ("FLAGS_skip_postinstall", sh_bool(self.skip_postinstall)),
            ("FLAGS_storage_diags", sh_bool(self.storage_diags)),
            ("FLAGS_lvm_stateful", sh_bool(lvm_stateful)),
            ("FLAGS_default_key_stateful", sh_bool(default_key_stateful)),
            ("FLAGS_minimal_copy", sh_bool(self.minimal_copy)),
            ("FLAGS_skip_gpt_creation", sh_bool(self.skip_gpt_creation)),
        ]);

        output.extend([
            ("FLAGS_dst", sh_path(&self.dst)),
            ("FLAGS_payload_image", sh_path(&self.payload_image)),
            ("FLAGS_pmbr_code", sh_path(&self.pmbr_code)),
            ("FLAGS_target_bios", sh_path(&self.target_bios)),
            ("FLAGS_lab_preserve_logs", sh_path(&self.lab_preserve_logs)),
        ]);

        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::ffi::OsString;

    #[test]
    fn test_lvm_stateful_arg() {
        let no_args = ["arg0"];
        let lvm = ["arg0", "--lvm_stateful"];
        let nolvm = ["arg0", "--nolvm_stateful"];
        // Test all possibilities, just to be thorough:
        // If default is true and no flags: lvm_stateful = true
        // If default is true and --lvm_stateful: lvm_stateful = true
        // If default is true and --nolvm_stateful: lvm_stateful = false
        // If default is false and no flags: lvm_stateful = false
        // If default is false and --lvm_stateful: lvm_stateful = true
        // If default is false and --nolvm_stateful: lvm_stateful = false
        assert_eq!(Args::parse_from(&no_args).lvm_stateful_arg(true), true);
        assert_eq!(Args::parse_from(&lvm).lvm_stateful_arg(true), true);
        assert_eq!(Args::parse_from(&nolvm).lvm_stateful_arg(true), false);
        assert_eq!(Args::parse_from(&no_args).lvm_stateful_arg(false), false);
        assert_eq!(Args::parse_from(&lvm).lvm_stateful_arg(false), true);
        assert_eq!(Args::parse_from(&nolvm).lvm_stateful_arg(false), false);

        // Can't specify both LVM flags at once
        let args = Args::try_parse_from(["arg0", "--lvm_stateful", "--nolvm_stateful"]);
        assert!(args.is_err());

        // Can't specify LVM flags and dm-default-key stateful support.
        let args = Args::try_parse_from(["arg0", "--lvm_stateful", "--default_key_stateful"]);
        assert!(args.is_err());
    }

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
