// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the logic used to setup sandboxes for TEE applications.

use std::os::unix::io::RawFd;
use std::path::Path;

use libc::pid_t;
use minijail::{self, Minijail};

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("failed to setup jail: {0}")]
    Jail(minijail::Error),
    #[error("failed to fork jail process: {0}")]
    ForkingJail(minijail::Error),
    #[error("failed to bind '{0}' to '{1}': {2}")]
    Bind(String, String, minijail::Error),
    #[error("failed to pivot root: {0}")]
    PivotRoot(minijail::Error),
    #[error("failed to parse seccomp policy: {0}")]
    SeccompPolicy(minijail::Error),
    #[error("failed to set max open files: {0}")]
    SettingMaxOpenFiles(minijail::Error),
    #[error("failed to wait on jailed process to complete: {0}")]
    Wait(minijail::Error),
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

const NEW_ROOT: &str = "/mnt/empty";

/// An abstraction for the TEE application sandbox.
pub struct Sandbox(minijail::Minijail);

impl Sandbox {
    /// Setup default sandbox / namespaces
    pub fn new(seccomp_bpf_file: Option<&Path>) -> Result<Self> {
        // All child jails run in a new user namespace without any users mapped,
        // they run as nobody unless otherwise configured.
        let mut j = Minijail::new().map_err(Error::Jail)?;

        j.namespace_pids();

        // TODO() determine why uid sandboxing doesn't work.
        //j.namespace_user();
        //j.namespace_user_disable_setgroups();

        j.change_user("nobody").unwrap();
        j.change_group("nobody").unwrap();

        j.use_caps(0);
        j.namespace_vfs();
        j.namespace_net();
        j.no_new_privs();

        if let Some(path) = seccomp_bpf_file {
            j.parse_seccomp_program(&path)
                .map_err(Error::SeccompPolicy)?;
            j.use_seccomp_filter();
        }

        let new_root = Path::new(NEW_ROOT);
        // The initramfs cannot be unmounted so bind mount /mnt/empty as read only and chroot.
        j.mount_bind(new_root, Path::new("/"), false)
            .map_err(|err| Error::Bind(NEW_ROOT.to_string(), NEW_ROOT.to_string(), err))?;
        j.enter_chroot(new_root).map_err(Error::PivotRoot)?;

        let limit = 1024u64;
        j.set_rlimit(libc::RLIMIT_NOFILE as i32, limit, limit)
            .map_err(Error::SettingMaxOpenFiles)?;

        Ok(Sandbox(j))
    }

    /// A version of the sandbox for use with tests because it doesn't require
    /// elevated privilege. It is also used for developer tools.
    pub fn passthrough() -> Result<Self> {
        let j = Minijail::new().map_err(Error::Jail)?;
        Ok(Sandbox(j))
    }

    /// Execute `cmd` with the specified arguments `args`. The specified file
    /// descriptors are connected to stdio for the child process.
    pub fn run(&mut self, cmd: &Path, args: &[&str], keep_fds: &[(RawFd, RawFd)]) -> Result<pid_t> {
        let pid = match self
            .0
            .run_remap(cmd, keep_fds, args)
            .map_err(Error::ForkingJail)?
        {
            0 => {
                unsafe { libc::exit(0) };
            }
            p => p,
        };

        Ok(pid)
    }

    /// Wait until the child process completes. Non-zero return codes are
    /// returned as an error.
    pub fn wait_for_completion(&mut self) -> Result<()> {
        self.0.wait().map_err(Error::Wait)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::io::{Read, Write};
    use std::os::unix::io::AsRawFd;

    use sys_util::pipe;

    use crate::transport::{CROS_CONNECTION_ERR_FD, CROS_CONNECTION_R_FD, CROS_CONNECTION_W_FD};

    fn do_test(mut s: Sandbox) {
        const STDOUT_TEST: &str = "stdout test";
        const STDERR_TEST: &str = "stderr test";

        let (r_stdin, mut w_stdin) = pipe(true).unwrap();
        let (mut r_stdout, w_stdout) = pipe(true).unwrap();
        let (mut r_stderr, w_stderr) = pipe(true).unwrap();

        let keep_fds: [(RawFd, RawFd); 3] = [
            (r_stdin.as_raw_fd(), CROS_CONNECTION_R_FD),
            (w_stdout.as_raw_fd(), CROS_CONNECTION_W_FD),
            (w_stderr.as_raw_fd(), CROS_CONNECTION_ERR_FD),
        ];

        println!("Starting sandboxed process.");
        s.run(Path::new("/bin/sh"), &["/bin/sh"], &keep_fds)
            .unwrap();
        std::mem::drop(r_stdin);
        std::mem::drop(w_stdout);
        std::mem::drop(w_stderr);

        println!("Writing to stdin.");
        // The sleep was added to possibly resolve https://crbug.com/1171078.
        write!(
            &mut w_stdin,
            "echo -n '{}'; sleep 0.05; echo -n '{}' 1>&2; exit;",
            STDOUT_TEST, STDERR_TEST
        )
        .unwrap();
        w_stdin.flush().unwrap();
        std::mem::drop(w_stdin);

        println!("Reading stdout.");
        let mut stdout_result = String::new();
        r_stdout.read_to_string(&mut stdout_result).unwrap();

        println!("Reading stderr.");
        let mut stderr_result = String::new();
        r_stderr.read_to_string(&mut stderr_result).unwrap();

        println!("Waiting for sandboxed process.");
        let result = s.wait_for_completion();

        println!("Validating result.");
        if result.is_err() {
            eprintln!("Got error code: {:?}", result)
        }

        assert_eq!(
            (stdout_result, stderr_result),
            (STDOUT_TEST.to_string(), STDERR_TEST.to_string())
        );

        result.unwrap();
    }

    #[test]
    #[ignore] // privileged operation.
    fn sandbox() {
        let s = Sandbox::new(None).unwrap();
        do_test(s);
    }

    #[test]
    fn sandbox_unpriviledged() {
        let s = Sandbox::passthrough().unwrap();
        do_test(s);
    }
}
