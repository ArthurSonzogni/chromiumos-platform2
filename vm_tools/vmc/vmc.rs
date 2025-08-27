// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod disk;
pub mod methods;
pub mod proto;

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::error::Error;
use std::fmt;
use std::fs;

use std::io::{stdin, stdout, BufRead, Write};

use getopts::Options;

use crate::disk::{DiskOpType, VmState};
use crate::methods::{ChromeOSError, ContainerSource, Methods, UserDisks, UserInfo, VmFeatures};
use system_api::cicerone_service::start_lxd_container_request::PrivilegeLevel;
use system_api::cicerone_service::VmDeviceAction;
use system_api::concierge_service::vm_info::VmType;

/// A string to string mapping of environment variables to values.
pub type EnvMap<'a> = BTreeMap<&'a str, &'a str>;

enum VmcError {
    Command(&'static str, Box<dyn Error>),
    BadProblemReportArguments(getopts::Fail),
    DiskOperation(String, Box<dyn Error>),
    ExpectedCrosUserIdHash,
    ExpectedUIntSize,
    ExpectedName,
    ExpectedNoArgs,
    ExpectedPath,
    ExpectedSize,
    ExpectedU8Bus,
    ExpectedU8Device,
    ExpectedU8Port,
    ExpectedUUID,
    ExpectedVmAndContainer,
    ExpectedVmAndFileName,
    ExpectedVmAndMaybeFileName,
    ExpectedVmAndPath,
    ExpectedVmAndSize,
    ExpectedVmBusDevice,
    ExpectedVmType,
    ExpectedVmDeviceUpdates,
    ExpectedVmHidrawDevice,
    ExpectedVmPort,
    InvalidVmDevice(String),
    InvalidVmDeviceAction(String),
    ExpectedPrivilegedFlagValue,
    UnknownSubcommand(String),
    ChromeboxUnknownSubcommand(String),
    UserCancelled,
}

use self::VmcError::*;

// Optional flag used with "vmc container" command. Use this with the getopts crate API.
static PRIVILEGED_FLAG: &str = "privileged";

// Option names for pvm.send-porblem-report command. Use this with the getopts crate API.
static EMAIL_OPTION: &str = "email";
static VM_NAME_OPTION: &str = "vm-name";

// Remove useless expression items that the `try_command!()` macro captures and stringifies when
// generating a `VmcError::Command`.
fn trim_routine(s: &str) -> String {
    // We are guaranteed to have at least one element after splitn()
    s.trim_start_matches("self.methods.")
        .split('(')
        .next()
        .unwrap()
        .to_string()
}

fn parse_disk_size(s: &str) -> Result<u64, VmcError> {
    match s.chars().last() {
        Some('M') => s[..s.len() - 1].parse::<u64>().map(|x| x * 1024 * 1024),
        Some('G') => s[..s.len() - 1]
            .parse::<u64>()
            .map(|x| x * 1024 * 1024 * 1024),
        _ => s.parse(),
    }
    .map_err(|_| ExpectedUIntSize)
}

impl fmt::Display for VmcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BadProblemReportArguments(e) => write!(f, "failed to parse arguments: {:?}", e),
            Command(routine, e) => {
                write!(f, "operation `{}` failed: {}", trim_routine(routine), e)
            }
            DiskOperation(op, e) => write!(f, "{} failed: {}", op, e),
            ExpectedCrosUserIdHash => write!(f, "expected CROS_USER_ID_HASH environment variable"),
            ExpectedUIntSize => write!(
                f,
                "expected unsigned integer for the disk size. (e.g. 1000000000, 256M, 1G)"
            ),
            ExpectedName => write!(f, "expected <name>"),
            ExpectedPath => write!(f, "expected <path>"),
            ExpectedSize => write!(f, "expected <size>"),
            ExpectedVmAndContainer => write!(
                f,
                "expected <vm name> <container name> [ <image server> <image alias> ]"
            ),
            ExpectedVmAndFileName => {
                write!(f, "expected <vm name> <file name> [removable storage name]")
            }
            ExpectedVmAndMaybeFileName => write!(
                f,
                "expected <vm name> [<file name> [removable storage name]]"
            ),
            ExpectedVmAndPath => write!(f, "expected <vm name> <path>"),
            ExpectedVmAndSize => write!(f, "expected <vm name> <size>"),
            ExpectedVmBusDevice => {
                write!(f, "expected <vm name> <bus>:<device> [<container name>]")
            }
            ExpectedVmType => write!(f, "VM Type information is required for this command"),
            ExpectedNoArgs => write!(f, "expected no arguments"),
            ExpectedU8Bus => write!(f, "expected <bus> to fit into an 8-bit integer"),
            ExpectedU8Device => write!(f, "expected <device> to fit into an 8-bit integer"),
            ExpectedU8Port => write!(f, "expected <port> to fit into an 8-bit integer"),
            ExpectedUUID => write!(f, "expected <command UUID>"),
            ExpectedVmDeviceUpdates => write!(f, "expected args `<device>:<enable|disable>`"),
            ExpectedVmHidrawDevice => {
                write!(f, "expected <vm name> <hidraw file>")
            }
            ExpectedVmPort => write!(f, "expected <vm name> <port>"),
            InvalidVmDevice(v) => write!(f, "invalid vm device {}", v),
            InvalidVmDeviceAction(a) => write!(f, "invalid vm device action {}", a),
            ExpectedPrivilegedFlagValue => {
                write!(f, "Expected <true/false> after the privileged flag")
            }
            UnknownSubcommand(s) => write!(f, "no such subcommand: `{}`", s),
            ChromeboxUnknownSubcommand(s) => {
                write!(f, "subcommand: `{}` only available on chromebox devices", s)
            }
            UserCancelled => write!(f, "cancelled by user"),
        }
    }
}

impl fmt::Debug for VmcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for VmcError {}

type VmcResult = Result<(), Box<dyn Error>>;

macro_rules! try_command {
    ($x:expr) => {
        if cfg!(test) {
            // Ignore the command's result for testing.
            $x.map_err(|e| Command(stringify!($x), e))
                .unwrap_or_default()
        } else {
            $x.map_err(|e| Command(stringify!($x), e))?
        }
    };
}

struct Command<'a, 'b, 'c> {
    methods: &'a mut Methods,
    args: &'b [&'b str],
    user_id_hash: &'c str,
    interactive: bool,
}

impl<'a, 'b, 'c> Command<'a, 'b, 'c> {
    // Metrics are on a best-effort basis. We print errors related to sending metrics, but stop
    // propagation of the error, which is why this function never returns an error.
    fn metrics_send_sample(&mut self, name: &str) {
        if let Err(e) = self.methods.metrics_send_sample(name) {
            eprintln!(
                "warning: failed attempt to send metrics sample `{}`: {}",
                name, e
            );
        }
    }

    fn user_id_hash_to_username(&mut self, user_id_hash: &str) -> Result<String, Box<dyn Error>> {
        if cfg!(test) {
            // Fake IPC result for the test environment.
            assert_eq!(user_id_hash, "fake_hash");
            Ok("fake_user".to_owned())
        } else {
            self.methods.user_id_hash_to_username(user_id_hash)
        }
    }

    fn start(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optflag("", "enable-gpu", "when starting the vm, enable gpu support");
        opts.optflag(
            "",
            "enable-dgpu-passthrough",
            "when starting the VM, enable discrete GPU passthrough support",
        );
        opts.optflag(
            "",
            "enable-big-gl",
            "when starting the vm, request Big GL renderer (implies --enable-gpu)",
        );
        opts.optflag(
            "",
            "enable-virtgpu-native-context",
            "when starting the vm, enable virtgpu native context support (implies --enable-gpu)",
        );
        opts.optflag("", "vtpm-proxy", "connect the virtio-tpm to vtpm daemon");
        opts.optflag(
            "",
            "enable-audio-capture",
            "when starting the vm, enable audio capture support",
        );
        opts.optopt("", "extra-disk", "path to an extra disk image.", "PATH");
        opts.optopt(
            "",
            "dlc-id",
            "Identifier for the DLC used to boot this VM.",
            "ID",
        );
        opts.optopt(
            "",
            "tools-dlc",
            "Identifier for the DLC from which guest tools should be pulled.",
            "ID",
        );
        opts.optopt("", "kernel", "path to a custom kernel image.", "PATH");
        opts.optopt("", "initrd", "path to a custom initrd.", "PATH");
        opts.optopt("", "rootfs", "path to a custom rootfs image.", "PATH");
        opts.optopt(
            "",
            "vm-type",
            "type of VM (CROSTINI / ARC_VM / BOREALIS / BRUSCHETTA / BAGUETTE)",
            "TYPE",
        );
        opts.optflag(
            "",
            "no-start-lxd",
            "Don't start LXD (the container manager)",
        );
        opts.optflag("", "writable-rootfs", "Mount the rootfs as writable.");
        opts.optmulti(
            "",
            "kernel-param",
            "Additional kernel cmdline parameter for the host.",
            "PARAM",
        );
        opts.optmulti(
            "",
            "oem-string",
            "Type 11 SMBIOS DMI OEM string to pass to the host.",
            "STRING",
        );
        opts.optopt("", "bios", "path to a custom bios image.", "PATH");
        opts.optopt("", "pflash", "path to a r/w bios flash image.", "PATH");
        opts.optopt(
            "",
            "bios-dlc",
            "Identifier for the DLC from which the bios should be pulled.",
            "ID",
        );
        opts.optopt("", "timeout", "seconds to wait until timeout.", "PARAM");
        opts.optflag("", "no-shell", "Don't start a shell in the started VM.");
        opts.optopt("", "user", "Sets up a non-root user in the VM.", "NAME");
        opts.optopt(
            "",
            "user-uid",
            "UID for the non-root user to be set up.",
            "PARAM",
        );
        opts.optopt(
            "",
            "user-groups",
            "Comma-separated additional groups for the non-root user to be set up.",
            "NAME,NAME,...",
        );
        opts.optflag("h", "help", "print this help menu");

        let matches = opts.parse(self.args)?;

        if matches.opt_present("help") {
            println!("{}", opts.usage("Usage: vmc start [options] <name>"));
            return Ok(());
        }

        if matches.free.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = &matches.free[0];
        let username = self.user_id_hash_to_username(self.user_id_hash)?;

        let big_gl = matches.opt_present("enable-big-gl");
        let virtgpu_native_context = matches.opt_present("enable-virtgpu-native-context");
        let gpu = virtgpu_native_context || big_gl || matches.opt_present("enable-gpu");
        let dgpu_passthrough = matches.opt_present("enable-dgpu-passthrough");
        let timeout = matches
            .opt_str("timeout")
            .map(|x| x.parse())
            .transpose()?
            .unwrap_or(0);

        let vm_type = match matches.opt_str("vm-type") {
            None => None,
            Some(vm_type) => Some(match vm_type.to_uppercase().as_ref() {
                // User-managed linux vm has default vm name of "termina", but this does not imply
                // a specific vm type.
                "CROSTINI" | "TERMINA" => VmType::TERMINA,
                "BOREALIS" => VmType::BOREALIS,
                "BRUSCHETTA" => VmType::BRUSCHETTA,
                "BAGUETTE" => VmType::BAGUETTE,
                _ => return Err(ChromeOSError::NoSuchVmType.into()),
            }),
        };
        let start_lxd = match vm_type {
            Some(VmType::BAGUETTE) => false,
            _ => !matches.opt_present("no-start-lxd"),
        };
        let tools_dlc_id = match (vm_type, matches.opt_str("tools-dlc")) {
            (Some(VmType::BAGUETTE), None) => Some("termina-tools-dlc".into()),
            (_, opt_tools_dlc) => opt_tools_dlc,
        };

        let features = VmFeatures {
            gpu,
            dgpu_passthrough,
            big_gl,
            virtgpu_native_context,
            vtpm_proxy: matches.opt_present("vtpm-proxy"),
            audio_capture: matches.opt_present("enable-audio-capture"),
            dlc: matches.opt_str("dlc-id"),
            kernel_params: matches.opt_strs("kernel-param"),
            tools_dlc_id,
            timeout,
            oem_strings: matches.opt_strs("oem-string"),
            bios_dlc_id: matches.opt_str("bios-dlc"),
            vm_type,
        };

        let user_disks = UserDisks {
            kernel: matches.opt_str("kernel"),
            rootfs: matches.opt_str("rootfs"),
            writable_rootfs: matches.opt_present("writable-rootfs"),
            extra_disk: matches.opt_str("extra-disk"),
            initrd: matches.opt_str("initrd"),
            bios: matches.opt_str("bios"),
            pflash: matches.opt_str("pflash"),
        };

        let group_names = match (vm_type, matches.opt_str("user-groups")) {
            (Some(VmType::BAGUETTE), None) => {
                Some("audio,cdrom,dialout,floppy,kvm,netdev,sudo,tss,video".into())
            }
            (_, group_names) => group_names,
        };
        let group_names = match group_names {
            Some(groups) => groups.split(',').map(|v| v.to_string()).collect(),
            None => vec![],
        };

        let user_info = match matches.opt_str("user") {
            Some(username) => Some(UserInfo {
                uid: matches.opt_str("user-uid").map(|x| x.parse()).transpose()?,
                username,
                group_names,
            }),
            None => None,
        };

        self.metrics_send_sample("Vm.VmcStart");
        try_command!(self.methods.vm_start(
            vm_name,
            self.user_id_hash,
            &username,
            features,
            user_disks,
            start_lxd,
            user_info,
            vm_type,
        ));
        self.metrics_send_sample("Vm.VmcStartSuccess");

        if !matches.opt_present("no-shell") {
            match vm_type {
                // Baguette is containerless but we pretend it has penguin.
                Some(VmType::BAGUETTE) => try_command!(self.methods.vsh_exec_container(
                    vm_name,
                    self.user_id_hash,
                    "penguin"
                )),
                _ => try_command!(self.methods.vsh_exec(vm_name, self.user_id_hash)),
            };
        }

        Ok(())
    }

    fn stop(&mut self) -> VmcResult {
        if self.args.contains(&"--help") || self.args.contains(&"-h") {
            println!("Usage: vmc stop <vm name>");
            return Ok(());
        }

        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];

        try_command!(self.methods.vm_stop(vm_name, self.user_id_hash));

        Ok(())
    }

    fn launch(&mut self) -> VmcResult {
        if self.args.is_empty() {
            return Err(ExpectedName.into());
        }
        try_command!(self.methods.vm_launch(self.user_id_hash, self.args));
        Ok(())
    }

    fn create(&mut self) -> VmcResult {
        let mut opts = Options::new();
        // By using StopAtFirstFree we allow this command to continue using `--`
        // as a separator for params which avoids breaking the existing
        // interface.
        opts.parsing_style(getopts::ParsingStyle::StopAtFirstFree);
        opts.optopt("", "vm-type", "type of VM (CROSTINI / BAGUETTE)", "TYPE");
        opts.optopt("", "size", "size of the created vm's disk", "SIZE");
        opts.optopt("", "source", "location of baguette source image", "PATH");
        opts.optflag("h", "help", "print this help menu");

        let matches = opts.parse(self.args)?;

        if matches.opt_present("help") {
            println!(
                "{}",
                opts.usage(
                    "Usage: vmc create [options] --vm-type <vm type> <vm name> \
            [<source media> [<removable storage name>]] [-- additional parameters]"
                )
            );
            return Ok(());
        }
        let size = match matches.opt_str("size") {
            Some(s) => Some(parse_disk_size(&s)?),
            None => None,
        };
        let vm_type = match matches.opt_str("vm-type") {
            None => return Err(VmcError::ExpectedVmType.into()),
            Some(vm_type) => match vm_type.to_uppercase().as_ref() {
                "CROSTINI" | "TERMINA" => VmType::TERMINA,
                "BAGUETTE" => VmType::BAGUETTE,
                _ => return Err(ChromeOSError::NoSuchVmType.into()),
            },
        };

        let mut s = matches.free.splitn(2, |arg| *arg == "--");
        let args = s.next().expect("failed to split argument list");
        let params = s.next().unwrap_or(&[]);

        let (vm_name, file_name, removable_media) = match args.len() {
            1 => (args[0].as_str(), None, None),
            2 => (args[0].as_str(), Some(args[1].as_str()), None),
            3 => (
                args[0].as_str(),
                Some(args[1].as_str()),
                Some(args[2].as_str()),
            ),
            _ => return Err(ExpectedVmAndMaybeFileName.into()),
        };

        if let Some(uuid) = try_command!(self.methods.vm_create(
            vm_name,
            self.user_id_hash,
            size,
            file_name,
            removable_media,
            params,
            matches.opt_str("source").as_deref(),
            vm_type
        )) {
            println!("VM creation in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, DiskOpType::Create, "VM creation")?;
        }
        Ok(())
    }

    fn adjust(&mut self) -> VmcResult {
        if self.args.len() < 2 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];
        let operation = &self.args[1];

        try_command!(self.methods.vm_adjust(
            vm_name,
            self.user_id_hash,
            operation,
            &self.args[2..]
        ));

        Ok(())
    }

    fn destroy(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optflag("y", "yes", "destroy without prompting");
        opts.optflag("h", "help", "print this help menu");

        let matches = opts.parse(self.args)?;

        if matches.opt_present("help") {
            println!("{}", opts.usage("Usage: vmc destroy [options] <vm name>"));
            return Ok(());
        }
        if matches.free.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = &matches.free[0];
        let skip_prompt = matches.opt_present("yes") || !self.interactive;

        if !skip_prompt {
            println!(
                "WARNING: this will delete all data stored in VM '{}'",
                vm_name
            );
            print!("Continue? (y/N) ");
            stdout().flush()?;

            let mut line = String::new();
            if stdin().lock().read_line(&mut line).is_err() {
                return Err(UserCancelled.into());
            }

            line = line.trim_end().to_string();

            if !(line == "y" || line == "yes") {
                return Err(UserCancelled.into());
            }
        }

        match self.methods.disk_destroy(vm_name, self.user_id_hash) {
            Ok(()) => Ok(()),
            Err(e) => {
                if cfg!(test) {
                    Ok(())
                } else {
                    self.metrics_send_sample("Vm.DiskEraseFailed");
                    Err(Command("disk_destroy", e).into())
                }
            }
        }
    }

    fn wait_disk_op_completion(
        &mut self,
        uuid: &str,
        op_type: DiskOpType,
        op_name: &str,
    ) -> VmcResult {
        let mut progress_reported = false;
        loop {
            match self.methods.wait_disk_op(uuid, self.user_id_hash, op_type) {
                Ok((done, progress)) => {
                    if done {
                        println!("\rOperation completed successfully");
                        return Ok(());
                    }

                    print!("\rOperation in progress: {}% done", progress);
                    stdout().flush()?;
                    progress_reported = true;
                }
                Err(e) => {
                    // Ignore the result for testing.
                    if cfg!(test) {
                        return Ok(());
                    }

                    if progress_reported {
                        println!();
                    }
                    return Err(DiskOperation(op_name.to_string(), e).into());
                }
            }
        }
    }

    fn resize(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndSize.into());
        }

        let vm_name = self.args[0];
        let size: u64 = self.args[1].parse().or(Err(ExpectedSize))?;

        match try_command!(self.methods.disk_resize(vm_name, self.user_id_hash, size)) {
            Some(uuid) => {
                println!("Resize in progress: {}", uuid);
                self.wait_disk_op_completion(&uuid, DiskOpType::Resize, "resize")?;
            }
            None => {
                println!("Operation completed successfully");
            }
        }
        Ok(())
    }

    fn export(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optflag(
            "d",
            "digest",
            "generate checksum/digest for the exported image",
        );
        opts.optflag(
            "f",
            "force",
            "force export even if VM is running or not shut down",
        );
        opts.optflag("h", "help", "print this help menu");

        let matches = opts.parse(self.args)?;

        if matches.opt_present("help") {
            println!(
                "{}",
                opts.usage(
                    "Usage: vmc export [options] <vm name> <file name> [<removable storage name>]"
                )
            );
            return Ok(());
        }

        let generate_digest = matches.opt_present("digest");
        let force = matches.opt_present("force");

        let (vm_name, file_name, removable_media) = match matches.free.len() {
            2 => (&matches.free[0], &matches.free[1], None),
            3 => (
                &matches.free[0],
                &matches.free[1],
                Some(matches.free[2].as_str()),
            ),
            _ => return Err(ExpectedVmAndFileName.into()),
        };

        let digest_name = file_name.to_owned() + ".sha256.txt";
        let digest_option = if generate_digest {
            Some(digest_name.as_str())
        } else {
            None
        };

        if let Some(uuid) = try_command!(self.methods.vm_export(
            vm_name,
            self.user_id_hash,
            file_name,
            digest_option,
            removable_media,
            force,
        )) {
            println!("Export in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, DiskOpType::Create, "export")?;
        }
        Ok(())
    }

    fn import(&mut self) -> VmcResult {
        let (vm_name, file_name, removable_media) = match self.args.len() {
            2 => (self.args[0], self.args[1], None),
            3 => (self.args[0], self.args[1], Some(self.args[2])),
            _ => return Err(ExpectedVmAndFileName.into()),
        };

        if let Some(uuid) = try_command!(self.methods.vm_import(
            vm_name,
            self.user_id_hash,
            file_name,
            removable_media
        )) {
            println!("Import in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, DiskOpType::Create, "import")?;
        }
        Ok(())
    }

    fn disk_op_status(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedUUID.into());
        }

        let uuid = self.args[0];

        let (done, progress) =
            try_command!(self
                .methods
                .disk_op_status(uuid, self.user_id_hash, DiskOpType::Create));
        if done {
            println!("Operation completed successfully");
        } else {
            println!("Operation in progress: {}% done", progress);
        }
        Ok(())
    }

    fn create_extra_disk(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optopt("", "size", "size of extra disk", "SIZE");
        opts.optflag("h", "help", "print this help menu");

        let matches = opts.parse(self.args)?;

        if matches.opt_present("help") {
            println!("{}", opts.usage("Usage: vmc create-extra-disk --size SIZE <file name> [<removable storage name>]"));
            return Ok(());
        }

        let s = matches.opt_str("size").ok_or_else(|| ExpectedSize)?;
        let size = parse_disk_size(&s)?;

        let (file_name, removable_media) = match matches.free.len() {
            1 => (&matches.free[0], None),
            2 => (&matches.free[0], Some(matches.free[1].as_str())),
            _ => return Err(ExpectedPath.into()),
        };

        let path = try_command!(self.methods.extra_disk_create(
            self.user_id_hash,
            file_name,
            removable_media,
            size
        ));
        println!("A raw disk is created at {}.", path.display());
        Ok(())
    }

    fn list(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        let (disk_image_list, total_size) = try_command!(self.methods.disk_list(self.user_id_hash));
        for disk in disk_image_list {
            let mut extra_info = String::new();
            if let Some(min_size) = disk.min_size {
                extra_info.push_str(&format!(", min shrinkable size {} bytes", min_size));
            }
            extra_info.push_str(&format!(", {}", disk.image_type));
            if !disk.user_chosen_size {
                extra_info.push_str(", sparse");
            }
            let state_str = match disk.state {
                VmState::Starting | VmState::Running => format!(" {}", disk.state),
                _ => String::new(),
            };
            let on_disk_vm_type_str = if disk.has_vm_type {
                format!("on disk VM type: {}", disk.vm_type)
            } else {
                "on disk VM type not specified".to_string()
            };
            println!(
                "{} ({} bytes{}) ({}) {}",
                disk.name, disk.size, extra_info, on_disk_vm_type_str, state_str
            );
        }
        println!("Total Size (bytes): {}", total_size);
        Ok(())
    }

    fn logs(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];

        let logs = try_command!(self.methods.get_vm_logs(vm_name, self.user_id_hash));
        print!("{}", logs);

        Ok(())
    }

    fn share(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndPath.into());
        }

        let vm_name = self.args[0];
        let path = self.args[1];
        let vm_path = try_command!(self.methods.vm_share_path(vm_name, self.user_id_hash, path));
        println!("{} is available at path {}", path, vm_path);
        Ok(())
    }

    fn unshare(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndPath.into());
        }

        let vm_name = self.args[0];
        let path = self.args[1];
        try_command!(self
            .methods
            .vm_unshare_path(vm_name, self.user_id_hash, path));
        Ok(())
    }

    fn container(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optopt(
            "p",
            PRIVILEGED_FLAG,
            "is the container privileged.",
            "true / false",
        );
        opts.optopt("", "timeout", "seconds to wait until timeout.", "PARAM");
        let matches = opts
            .parse(self.args)
            .map_err(|_| ExpectedPrivilegedFlagValue)?;

        // The privileged flag is optional but when its given it must be followed by a valid value.
        let privilege_level = match matches.opt_str(PRIVILEGED_FLAG) {
            Some(s) => match s.as_str() {
                "True" | "true" => PrivilegeLevel::PRIVILEGED,
                "False" | "false" => PrivilegeLevel::UNPRIVILEGED,
                _ => return Err(ExpectedPrivilegedFlagValue.into()),
            },
            None => PrivilegeLevel::UNCHANGED,
        };

        let timeout = matches.opt_str("timeout").map(|x| x.parse()).transpose()?;

        let required_args = &matches.free;
        let (vm_name, container_name, source) = match required_args.len() {
            2 => (
                required_args[0].as_str(),
                required_args[1].as_str(),
                ContainerSource::ImageServer {
                    image_server: "https://storage.googleapis.com/cros-containers/%d".to_string(),
                    image_alias: "debian/bookworm".to_string(),
                },
            ),
            4 => (
                required_args[0].as_str(),
                required_args[1].as_str(),
                // If this argument looks like an absolute path, treat it and the following
                // parameter as local paths to tarballs.  Otherwise, assume they are an
                // image server URL and image alias.
                if required_args[2].starts_with('/') {
                    ContainerSource::Tarballs {
                        rootfs_path: required_args[2].clone(),
                        metadata_path: required_args[3].clone(),
                    }
                } else {
                    ContainerSource::ImageServer {
                        image_server: required_args[2].clone(),
                        image_alias: required_args[3].clone(),
                    }
                },
            ),
            _ => return Err(ExpectedVmAndContainer.into()),
        };

        let username = self.user_id_hash_to_username(self.user_id_hash)?;

        try_command!(self.methods.container_create(
            vm_name,
            self.user_id_hash,
            container_name,
            source,
            timeout
        ));
        try_command!(self.methods.container_setup_user(
            vm_name,
            self.user_id_hash,
            container_name,
            &username
        ));

        // If the container was already running then this will update the privilege level of the
        // container if |privilege_level| is not |UNCHANGED|. This will take into effect on next
        // container boot.
        try_command!(self.methods.container_start(
            vm_name,
            self.user_id_hash,
            container_name,
            privilege_level,
            timeout
        ));

        try_command!(self
            .methods
            .vsh_exec_container(vm_name, self.user_id_hash, container_name));

        Ok(())
    }

    fn update_container_devices(&mut self) -> VmcResult {
        let (vm_name, container_name) = match self.args.len() {
            0 | 1 => return Err(ExpectedVmAndPath.into()),
            2 => return Err(ExpectedVmDeviceUpdates.into()),
            _ => (self.args[0], self.args[1]),
        };

        let mut updates = HashMap::<String, VmDeviceAction>::new();

        for u in &self.args[2..] {
            let update_parts: Vec<&str> = u.splitn(2, ':').collect();
            match update_parts.len() {
                2 => {
                    println!("{:?}", update_parts);
                    match (update_parts[0], update_parts[1]) {
                        ("", &_) => return Err(InvalidVmDevice("<empty>".to_string()).into()),
                        (d, "enable") => updates.insert(d.to_string(), VmDeviceAction::ENABLE),
                        (d, "disable") => updates.insert(d.to_string(), VmDeviceAction::DISABLE),
                        (_, a) => return Err(InvalidVmDeviceAction(a.to_string()).into()),
                    }
                }
                _ => return Err(InvalidVmDeviceAction(u.to_string()).into()),
            };
        }

        let res = try_command!(self.methods.container_update_devices(
            vm_name,
            self.user_id_hash,
            container_name,
            &updates
        ));
        println!("{}", res);
        Ok(())
    }

    fn usb_attach(&mut self) -> VmcResult {
        let (vm_name, bus_device, container_name) = match self.args.len() {
            3 => (self.args[0], self.args[1], Some(self.args[2])),
            2 => (self.args[0], self.args[1], None),
            _ => return Err(ExpectedVmBusDevice.into()),
        };

        let mut bus_device_parts = bus_device.splitn(2, ':');
        let (bus, device) = match (bus_device_parts.next(), bus_device_parts.next()) {
            (Some(bus_str), Some(device_str)) => (
                bus_str.parse().or(Err(ExpectedU8Bus))?,
                device_str.parse().or(Err(ExpectedU8Device))?,
            ),
            _ => return Err(ExpectedVmBusDevice.into()),
        };

        let guest_port = try_command!(self.methods.usb_attach(
            vm_name,
            self.user_id_hash,
            bus,
            device,
            container_name
        ));

        if let Some(container) = container_name {
            println!(
                "usb device at bus={} device={} attached to container {}:{} at port={}",
                bus, device, vm_name, container, guest_port
            );
        } else {
            println!(
                "usb device at bus={} device={} attached to vm {} at port={}",
                bus, device, vm_name, guest_port
            );
        }

        Ok(())
    }

    fn allow_all_io_devices(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        try_command!(self.methods.unset_primary_mouse());
        try_command!(self.methods.unset_primary_keyboard());

        Ok(())
    }

    fn list_primary_io_devices(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        let primary_devices = try_command!(self.methods.list_primary_io_devices());

        for device in primary_devices {
            println!("{}", device)
        }

        Ok(())
    }

    fn unset_primary_mouse(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        try_command!(self.methods.unset_primary_mouse());

        Ok(())
    }

    fn unset_primary_keyboard(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        try_command!(self.methods.unset_primary_keyboard());

        Ok(())
    }

    fn key_attach(&mut self) -> VmcResult {
        // TODO(morg): need to implement command for sharing security key with container too
        let (vm_name, hidraw_device) = match self.args.len() {
            2 => (self.args[0], self.args[1]),
            _ => return Err(ExpectedVmHidrawDevice.into()),
        };

        let guest_port =
            try_command!(self
                .methods
                .key_attach(vm_name, self.user_id_hash, hidraw_device,));

        println!(
            "Security key at {} shared with vm {} at port={}",
            hidraw_device, vm_name, guest_port
        );
        Ok(())
    }

    fn usb_detach(&mut self) -> VmcResult {
        let (vm_name, port) = match self.args.len() {
            2 => (self.args[0], self.args[1].parse().or(Err(ExpectedU8Port))?),
            _ => return Err(ExpectedVmPort.into()),
        };

        try_command!(self.methods.usb_detach(vm_name, self.user_id_hash, port));

        println!("usb device detached from port {}", port);

        Ok(())
    }

    fn usb_list(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];

        let devices = try_command!(self.methods.usb_list(vm_name, self.user_id_hash));
        if devices.is_empty() {
            println!("No attached usb devices");
        }
        for (port, vendor_id, product_id, name) in devices {
            println!(
                "Port {:03} ID {:04x}:{:04x} {}",
                port, vendor_id, product_id, name
            );
        }

        Ok(())
    }

    fn pvm_send_problem_report(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optopt(
            "e",
            EMAIL_OPTION,
            "email to associate with the problem report",
            "EMAIL",
        );
        opts.optopt(
            "n",
            VM_NAME_OPTION,
            "name of the VM for which problem report is generated",
            "NAME",
        );
        let matches = opts.parse(self.args).map_err(BadProblemReportArguments)?;

        let vm_name = matches.opt_str(VM_NAME_OPTION);
        let email = matches.opt_str(EMAIL_OPTION);
        let text = if matches.free.is_empty() {
            None
        } else {
            Some(matches.free.join(" "))
        };

        let report_id = try_command!(self.methods.pvm_send_problem_report(
            vm_name,
            self.user_id_hash,
            email,
            text
        ));

        println!("Problem report has been sent. Report ID: {}", report_id);
        Ok(())
    }

    fn inspect_backup(&mut self) -> VmcResult {
        let (file_path, removable_media) = match self.args.len() {
            1 => (self.args[0], None),
            2 => (self.args[0], Some(self.args[1])),
            _ => return Err(ExpectedPath.into()),
        };

        let vm_type = try_command!(self.methods.inspect_backup_file(
            self.user_id_hash,
            file_path,
            removable_media
        ));
        println!("VM type of this backup is: {vm_type}");
        Ok(())
    }
}

const USAGE: &str = " [
     start [--enable-gpu] [--enable-dgpu-passthrough] [--enable-big-gl] \
           [--enable-virtgpu-native-context] [--vtpm-proxy] \
           [--enable-audio-capture] [--extra-disk PATH] [--dlc ID] \
           [--tools-dlc ID] [--kernel PATH] [--initrd PATH] [--rootfs PATH] \
           [--vm-type <CROSTINI | ARC_VM | BOREALIS | BRUSCHETTA | BAGUETTE>] \
           [--no-start-lxd] [--writable-rootfs] [--kernel-param PARAM]... \
           [--oem-string STRING]... [--bios PATH] [--pflash PATH] [--bios-dlc ID] \
           [--timeout PARAM] [--no-shell] [--user NAME] [--user-uid PARAM] \
           [--user-group PARAM]... <vm name>
  |  stop <vm name>
  |  launch <main descriptor> [<descriptor>...]
  |  create [-p] [--size SIZE] --vm-type <vm type> <vm name> \
            [<source media> [<removable storage name>]] [-- additional parameters]
  |  create-extra-disk --size SIZE <file name> [<removable storage name>]
  |  adjust <vm name> <operation> [additional parameters]
  |  destroy [-y] <vm name>
  |  disk-op-status <command UUID>
  |  export [-d] [-f] <vm name> <file name> [<removable storage name>]
  |  import [-p] <vm name> <file name> [<removable storage name>]
  |  inspect-backup <file name> [<removable storage name>]
  |  resize <vm name> <size>
  |  list
  |  logs <vm name>
  |  share <vm name> <path>
  |  unshare <vm name> <path>
  |  container <vm name> <container name> [ (<image server> <image alias>) | \
                                            (<rootfs path> <metadata path>) ] \
                                          [--privileged <true/false>] [--timeout PARAM]
  |  update-container-devices <vm name> <container name> (<vm device>:<enable/disable>)...
  |  usb-attach <vm name> <bus>:<device> [<container name>]
  |  usb-detach <vm name> <port>
  |  usb-list <vm name>
  |  key-attach <vm name> <hidraw path>
  |  pvm.send-problem-report [-n <vm name>] [-e <reporter's email>] <description of the problem>";
const USAGE_ON_CHROMEBOX: &str = "
  |  allow-all-io-devices [on chromeboxes, allow all keyboards/mice to connect]
  |  list-primary-io-devices
  |  unset-primary-keyboard
  |  unset-primary-mouse";
const USAGE_END: &str = "
  |  --help | -h
]";

/// `vmc` (Virtual Machine Controller) command line interface.
/// This is the interface accessible from crosh (Ctrl-Alt-T in the browser to access).
pub struct Vmc<'a> {
    user_id_hash: &'a str,
    interactive: bool,
    running_on_chromebox: bool,
}

impl Vmc<'_> {
    fn print_usage(&self, program_name: &str) {
        eprintln!(
            "USAGE: {}{}{}{}",
            program_name,
            USAGE,
            if self.running_on_chromebox {
                USAGE_ON_CHROMEBOX
            } else {
                ""
            },
            USAGE_END
        );
    }

    fn try_chromebox_command(
        &self,
        mut f: impl FnMut() -> VmcResult,
        command_name: &str,
    ) -> VmcResult {
        if !self.running_on_chromebox {
            return Err(ChromeboxUnknownSubcommand(command_name.to_owned()).into());
        }
        f()
    }

    fn run(&self, methods: &mut Methods, args: &[&str]) -> VmcResult {
        if args.len() < 2 {
            self.print_usage("vmc");
            return Ok(());
        }

        let mut command = Command {
            methods,
            args: &args[2..],
            user_id_hash: self.user_id_hash,
            interactive: self.interactive,
        };

        let command_name = args[1];
        match command_name {
            "start" => command.start(),
            "stop" => command.stop(),
            "launch" => command.launch(),
            "create" => command.create(),
            "create-extra-disk" => command.create_extra_disk(),
            "adjust" => command.adjust(),
            "destroy" => command.destroy(),
            "export" => command.export(),
            "import" => command.import(),
            "disk-op-status" => command.disk_op_status(),
            "resize" => command.resize(),
            "list" => command.list(),
            "logs" => command.logs(),
            "share" => command.share(),
            "unshare" => command.unshare(),
            "container" => command.container(),
            "update-container-devices" => command.update_container_devices(),
            "usb-attach" => command.usb_attach(),
            "usb-detach" => command.usb_detach(),
            "usb-list" => command.usb_list(),
            "key-attach" => command.key_attach(),
            "pvm.send-problem-report" => command.pvm_send_problem_report(),
            "inspect-backup" => command.inspect_backup(),
            "allow-all-io-devices" => {
                self.try_chromebox_command(|| command.allow_all_io_devices(), command_name)
            }
            "list-primary-io-devices" => {
                self.try_chromebox_command(|| command.list_primary_io_devices(), command_name)
            }
            "unset-primary-mouse" => {
                self.try_chromebox_command(|| command.unset_primary_mouse(), command_name)
            }
            "unset-primary-keyboard" => {
                self.try_chromebox_command(|| command.unset_primary_keyboard(), command_name)
            }
            _ => {
                if command_name == "--help" || command_name == "-h" {
                    self.print_usage("vmc");
                    return Ok(());
                }
                Err(UnknownSubcommand(command_name.to_owned()).into())
            }
        }
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    libchromeos::panic_handler::install_memfd_handler();
    let args_string: Vec<String> = std::env::args().collect();
    let args: Vec<&str> = args_string.iter().map(|s| s.as_str()).collect();

    let interactive = std::env::var("VMC_NONINTERACTIVE").is_err();

    // When running inside crosh, `CROS_USER_ID_HASH` will be set in the environment.
    // Otherwise ask SessionManager for the primary session ID.
    let user_id_hash = if let Ok(hash) = std::env::var("CROS_USER_ID_HASH") {
        hash
    } else {
        libchromeos::chromeos::get_user_id_hash().map_err(|_| ExpectedCrosUserIdHash)?
    };

    let form_factor = fs::read_to_string("/run/chromeos-config/v1/hardware-properties/form-factor")
        .unwrap_or("".to_string());
    let running_on_chromebox =
        form_factor == "CHROMEBOX" || /* unknown case */ form_factor.is_empty();

    let vmc = Vmc {
        user_id_hash: &user_id_hash,
        interactive,
        running_on_chromebox,
    };
    vmc.run(&mut Methods::new()?, &args)
}

#[cfg(test)]
mod tests {
    use super::*;
    use dbus::Message;
    use protobuf::Message as ProtoMessage;
    use std::collections::HashMap;
    use system_api::concierge_service::*;
    use system_api::dlcservice::*;

    fn mocked_connection_filter(mut msg: Message) -> Result<Message, Result<Message, dbus::Error>> {
        eprintln!("{:?}", msg);
        msg.set_serial(1);
        if let Some(member) = msg.member() {
            match member.into_cstring().to_bytes() {
                b"GetDlcState" => {
                    let mut dlc_state = DlcState::new();
                    dlc_state.state = dlc_state::State::INSTALLED.into();
                    let msg_return = msg
                        .method_return()
                        .append1(dlc_state.write_to_bytes().unwrap());
                    return Err(Ok(msg_return));
                }
                b"StartVmPluginDispatcher" => {
                    let msg_return = msg.method_return().append1(true);
                    return Err(Ok(msg_return));
                }
                b"DestroyDiskImage" => {
                    let mut resp = DestroyDiskImageResponse::new();
                    resp.status = DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST.into();
                    let msg_return = msg.method_return().append1(resp.write_to_bytes().unwrap());
                    return Err(Ok(msg_return));
                }
                b"RetrieveActiveSessions" => {
                    let sessions: HashMap<String, String> =
                        [("testuser@example.com".to_owned(), "fake_hash".to_owned())]
                            .iter()
                            .cloned()
                            .collect();
                    let msg_return = msg.method_return().append1(sessions);
                    return Err(Ok(msg_return));
                }
                b"ListVmDisks" => {
                    let mut resp = ListVmDisksResponse::new();
                    resp.images.push(VmDiskInfo {
                        name: "PvmDefault".to_owned(),
                        ..Default::default()
                    });
                    resp.success = true;
                    let msg_return = msg.method_return().append1(resp.write_to_bytes().unwrap());
                    return Err(Ok(msg_return));
                }
                _ => {}
            }
        }
        Ok(msg)
    }

    fn mocked_methods() -> Methods {
        let mut methods = Methods::dummy();
        methods
            .connection_proxy_mut()
            .set_filter(mocked_connection_filter);
        methods
    }

    #[test]
    fn arg_parsing() {
        const DUMMY_SUCCESS_ARGS: &[&[&str]] = &[
            &["vmc", "start", "termina"],
            &["vmc", "start", "--vm-type", "crostini", "termina"],
            &["vmc", "start", "--vm-type", "baguette", "termina"],
            &["vmc", "start", "--enable-gpu", "termina"],
            &["vmc", "start", "termina", "--enable-gpu"],
            &["vmc", "start", "termina", "--enable-big-gl"],
            &["vmc", "start", "termina", "--enable-gpu", "--enable-big-gl"],
            &["vmc", "start", "termina", "--enable-virtgpu-native-context"],
            &[
                "vmc",
                "start",
                "termina",
                "--enable-gpu",
                "--enable-virtgpu-native-context",
            ],
            &["vmc", "start", "termina", "--vtpm-proxy"],
            &["vmc", "start", "termina", "--enable-audio-capture"],
            &[
                "vmc",
                "start",
                "termina",
                "--enable-audio-capture",
                "--enable-gpu",
            ],
            &[
                "vmc",
                "start",
                "termina",
                "--enable-gpu",
                "--enable-audio-capture",
            ],
            &["vmc", "start", "termina", "--extra-disk=foo.img"],
            &["vmc", "start", "termina", "--extra-disk", "foo.img"],
            &[
                "vmc",
                "start",
                "termina",
                "--extra-disk",
                "foo.img",
                "--enable-audio-capture",
                "--enable-gpu",
            ],
            &["vmc", "start", "termina", "--no-start-lxd"],
            &["vmc", "start", "termina", "--dlc-id=foo"],
            &["vmc", "start", "termina", "--initrd=myinitrd"],
            &["vmc", "start", "termina", "--initrd", "myinitrd"],
            &["vmc", "start", "termina", "--rootfs", "myrootfs"],
            &["vmc", "start", "termina", "--rootfs=myrootfs"],
            &["vmc", "start", "termina", "--timeout", "3"],
            &[
                "vmc",
                "start",
                "termina",
                "--rootfs",
                "myrootfs",
                "--writable-rootfs",
            ],
            &["vmc", "start", "termina", "--kernel-param=quiet"],
            &["vmc", "start", "termina", "--kernel-param", "quiet"],
            &[
                "vmc",
                "start",
                "termina",
                "--kernel-param=quiet",
                "--kernel-param=slub_debug",
            ],
            &[
                "vmc",
                "start",
                "termina",
                "--kernel-param",
                "quiet",
                "--kernel-param",
                "slub_debug",
            ],
            &[
                "vmc",
                "start",
                "termina",
                "--kernel-param",
                "quiet slub_debug",
            ],
            &["vmc", "start", "--kernel-param", "quiet", "termina"],
            &["vmc", "start", "--oem-string=my-oem-string-1", "termina"],
            &["vmc", "start", "--oem-string", "my-oem-string-1", "termina"],
            &[
                "vmc",
                "start",
                "--oem-string=my-oem-string-1",
                "--oem-string=my-oem-string-2",
                "termina",
            ],
            &[
                "vmc",
                "start",
                "--oem-string",
                "my-oem-string-1",
                "--oem-string",
                "my-oem-string-2",
                "termina",
            ],
            &["vmc", "start", "--bios", "mybios", "termina"],
            &["vmc", "start", "--bios=mybios", "termina"],
            &["vmc", "start", "--tools-dlc", "my-dlc", "termina"],
            &["vmc", "start", "--tools-dlc=my-dlc", "termina"],
            &["vmc", "stop", "termina"],
            &["vmc", "launch", "foo"],
            &["vmc", "launch", "a", "b", "c", "d", "e", "f"],
            &["vmc", "create", "--vm-type", "baguette", "termina"],
            &[
                "vmc",
                "create",
                "--vm-type",
                "crostini",
                "--size",
                "1000000",
                "termina",
            ],
            &[
                "vmc",
                "create",
                "--vm-type",
                "baguette",
                "--size",
                "256M",
                "termina",
            ],
            &[
                "vmc",
                "create",
                "--vm-type",
                "crostini",
                "--size",
                "1G",
                "termina",
            ],
            &["vmc", "create-extra-disk", "--size=1000000", "foo.img"],
            &["vmc", "create-extra-disk", "--size=256M", "foo.img"],
            &["vmc", "create-extra-disk", "--size=1G", "foo.img"],
            &["vmc", "create-extra-disk", "--size", "1G", "foo.img"],
            &[
                "vmc",
                "create-extra-disk",
                "--size=1G",
                "foo.img",
                "USB Drive",
            ],
            &[
                "vmc",
                "update-container-devices",
                "termina",
                "penguin",
                "microphone:disable",
            ],
            &[
                "vmc",
                "update-container-devices",
                "termina",
                "penguin",
                "microphone:enable",
                "camera:disable",
            ],
            &["vmc", "adjust", "termina", "op"],
            &["vmc", "adjust", "termina", "op", "param"],
            &["vmc", "destroy", "termina"],
            &["vmc", "destroy", "-y", "termina"],
            &["vmc", "disk-op-status", "12345"],
            &["vmc", "export", "termina", "file name"],
            &["vmc", "export", "-d", "termina", "file name"],
            &["vmc", "export", "-d", "-f", "termina", "file name"],
            &["vmc", "export", "termina", "file name", "removable media"],
            &[
                "vmc",
                "export",
                "-d",
                "termina",
                "file name",
                "removable media",
            ],
            &["vmc", "import", "termina", "file name"],
            &["vmc", "import", "termina", "file name", "removable media"],
            &["vmc", "list"],
            &["vmc", "logs", "cowcat"],
            &["vmc", "share", "termina", "my-folder"],
            &["vmc", "unshare", "termina", "my-folder"],
            &["vmc", "usb-attach", "termina", "1:2"],
            &["vmc", "usb-attach", "termina", "1:2", "penguin"],
            &["vmc", "usb-detach", "termina", "5"],
            &["vmc", "usb-detach", "termina", "5"],
            &["vmc", "usb-list", "termina"],
            &["vmc", "pvm.send-problem-report"],
            &["vmc", "pvm.send-problem-report", "text"],
            &["vmc", "pvm.send-problem-report", "text", "text2"],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-n",
                "termina",
                "text",
                "text2",
            ],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-e",
                "someone@somewhere.com",
                "text",
                "text2",
            ],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-n",
                "termina",
                "-e",
                "someone@somewhere.com",
                "text",
                "text2",
            ],
            &["vmc", "--help"],
            &["vmc", "-h"],
        ];

        const DUMMY_FAILURE_ARGS: &[&[&str]] = &[
            &["vmc", "start"],
            &["vmc", "start", "--i-made-this-up", "termina"],
            &["vmc", "start", "termina", "extra args"],
            &["vmc", "start", "termina", "--extra-disk"],
            &["vmc", "start", "termina", "--dlc-id"],
            &["vmc", "start", "termina", "--initrd"],
            &["vmc", "start", "termina", "--rootfs"],
            &["vmc", "start", "termina", "--writable-rootfs", "myrootfs"],
            &["vmc", "start", "termina", "--kernel-param"],
            &["vmc", "start", "termina", "--oem-string"],
            &["vmc", "start", "termina", "--bios"],
            &["vmc", "start", "termina", "--tools-dlc"],
            &["vmc", "start", "termina", "--timeout"],
            &["vmc", "start", "termina", "--timeout", "xyz"],
            &["vmc", "start", "termina", "--bios-dlc"],
            &["vmc", "stop"],
            &["vmc", "stop", "termina", "extra args"],
            &["vmc", "launch"],
            &["vmc", "create", "termina"],
            &["vmc", "create", "--size", "1000000", "termina"],
            &["vmc", "create", "--size", "256M", "termina"],
            &["vmc", "create", "--size", "1G", "termina"],
            &["vmc", "create"],
            &["vmc", "create", "--vm-type"],
            &[
                "vmc",
                "create",
                "--vm-type",
                "baguette",
                "--size",
                "termina",
            ],
            &["vmc", "create", "--size", "52J", "termina"],
            &[
                "vmc",
                "create",
                "--vm-type",
                "crostini",
                "--size",
                "foo",
                "termina",
            ],
            &["vmc", "create-extra-disk"],
            &["vmc", "create-extra-disk", "foo.img"],
            &["vmc", "create-extra-disk", "--size", "1G"],
            &["vmc", "create-extra-disk", "--size", "foo.img"],
            &["vmc", "create-extra-disk", "--size=1G"],
            &[
                "vmc",
                "update-container-devices",
                "termina",
                "penguin",
                "microphone:eat",
                "camera:enable",
            ],
            &[
                "vmc",
                "update-container-devices",
                "termina",
                "penguin",
                ":enable",
            ],
            &[
                "vmc",
                "update-container-devices",
                "termina",
                "penguin",
                "enable:",
            ],
            &["vmc", "adjust"],
            &["vmc", "adjust", "termina"],
            &["vmc", "destroy"],
            &["vmc", "destroy", "termina", "extra args"],
            &["vmc", "disk-op-status"],
            &["vmc", "destroy", "12345", "extra args"],
            &["vmc", "export", "termina"],
            &["vmc", "export", "-d", "termina"],
            &["vmc", "export", "termina", "too", "many", "args"],
            &["vmc", "export", "-d", "termina", "too", "many", "args"],
            &[
                "vmc", "export", "-d", "-f", "termina", "too", "many", "args",
            ],
            &["vmc", "import", "termina"],
            &["vmc", "import", "termina", "too", "many", "args"],
            &["vmc", "list", "extra args"],
            &["vmc", "logs"],
            &["vmc", "logs", "too", "many args"],
            &["vmc", "share"],
            &["vmc", "share", "too", "many", "args"],
            &["vmc", "unshare"],
            &["vmc", "unshare", "too", "many", "args"],
            &["vmc", "usb-attach"],
            &["vmc", "usb-attach", "termina"],
            &["vmc", "usb-attach", "termina", "whatever"],
            &["vmc", "usb-attach", "termina", "1:2:1dee:93d2"],
            &["vmc", "usb-attach", "termina", "whatever", "whatever"],
            &["vmc", "usb-attach", "termina", "1:2", "penguin", "whatever"],
            &["vmc", "usb-detach"],
            &["vmc", "usb-detach", "not-a-number"],
            &["vmc", "usb-list"],
            &["vmc", "usb-list", "termina", "args"],
            &["vmc", "pvm.send-problem-report", "-e"],
            &["vmc", "pvm.send-problem-report", "-n"],
        ];

        let mut methods = mocked_methods();

        let vmc = Vmc {
            user_id_hash: "fake_hash",
            interactive: false,
            running_on_chromebox: false,
        };

        for args in DUMMY_SUCCESS_ARGS {
            if let Err(e) = vmc.run(&mut methods, args) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }
        for args in DUMMY_FAILURE_ARGS {
            if let Ok(()) = vmc.run(&mut methods, args) {
                panic!("test args should have failed: {:?}", args)
            }
        }
    }

    #[test]
    fn container() {
        const CONTAINER_ARGS: &[&[&str]] = &[
            &["vmc", "container", "termina", "penguin"],
            &[
                "vmc",
                "container",
                "termina",
                "penguin",
                "https://my-image-server.com/",
                "custom/os",
            ],
        ];

        // How |PRIVILEGED_FLAG| appears on the command line.
        const PRIVILEGED_FLAG_CMDLINE: &str = "--privileged";

        let mut methods = mocked_methods();

        let vmc = Vmc {
            user_id_hash: "fake_hash",
            interactive: false,
            running_on_chromebox: false,
        };

        for args in CONTAINER_ARGS {
            if let Err(e) = vmc.run(&mut methods, args) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }

        // Test "--privileged" flag.
        const DUMMY_PRIVILEGED_SUCCESS_ARGS: &[&[&str]] = &[
            &[
                "vmc",
                "container",
                "termina",
                "penguin",
                PRIVILEGED_FLAG_CMDLINE,
                "true",
            ],
            &[
                "vmc",
                "container",
                "termina",
                PRIVILEGED_FLAG_CMDLINE,
                "false",
                "penguin",
            ],
            &[
                "vmc",
                "container",
                PRIVILEGED_FLAG_CMDLINE,
                "true",
                "termina",
                "penguin",
            ],
        ];

        for args in DUMMY_PRIVILEGED_SUCCESS_ARGS {
            if let Err(e) = vmc.run(&mut methods, args) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }

        const DUMMY_PRIVILEGED_FAILURE_ARGS: &[&[&str]] = &[
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE],
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE, "termina"],
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE, "termina"],
            &[
                "vmc",
                "container",
                PRIVILEGED_FLAG_CMDLINE,
                "termina",
                "penguin",
            ],
        ];

        for args in DUMMY_PRIVILEGED_FAILURE_ARGS {
            if let Ok(()) = vmc.run(&mut methods, args) {
                panic!("test args should have failed: {:?}", args)
            }
        }

        let args = &["vmc", "container", "termina", "a", "--timeout", "600"];
        if let Err(e) = vmc.run(&mut methods, args) {
            panic!("test args failed: {:?}: {}", args, e)
        }
    }
}
