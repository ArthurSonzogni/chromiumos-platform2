// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Exports dependencies which are locked to `crosvm_base` versions. This allows us to have a single
// point of declaration for these, rather than N.
pub mod sys_deps {
    pub use zerocopy;
}

#[cfg(feature = "chromeos-module")]
pub mod chromeos;

// Fallback dev-mode check if vboot_reference is not available.
#[cfg(not(feature = "chromeos-module"))]
pub mod chromeos {
    use std::fs::read_to_string;
    use std::io;
    use std::path::Path;

    use thiserror::Error as ThisError;

    #[derive(ThisError, Debug)]
    pub enum Error {
        #[error("failed to get kernel command line: {0}")]
        ReadError(io::Error),
    }

    pub type Result<R> = std::result::Result<R, Error>;

    pub fn is_dev_mode() -> Result<bool> {
        let contents = read_to_string(Path::new("/proc/cmdline")).map_err(Error::ReadError)?;
        Ok(contents.split(' ').any(|token| token == "cros_debug"))
    }
}

pub mod deprecated;
pub mod panic_handler;
pub mod rand;
pub mod scoped_path;
pub mod secure_blob;
pub mod signal;
pub mod syslog;

use std::fs::File;
use std::os::unix::io::FromRawFd;

use nix::fcntl::OFlag;

/// Spawns a pipe pair where the first pipe is the read end and the second pipe is the write end.
///
/// If `close_on_exec` is true, the `O_CLOEXEC` flag will be set during pipe creation.
pub fn pipe(close_on_exec: bool) -> nix::Result<(File, File)> {
    let flags = if close_on_exec {
        OFlag::O_CLOEXEC
    } else {
        OFlag::empty()
    };
    // Safe because the file descriptors aren't owned yet.
    nix::unistd::pipe2(flags).map(|(a, b)| unsafe { (File::from_raw_fd(a), File::from_raw_fd(b)) })
}

#[macro_export]
macro_rules! handle_eintr_errno {
    ($x:expr) => {{
        use libc::EINTR;
        use nix::errno::errno;

        let mut res;
        loop {
            res = $x;
            if res != -1 || errno() != EINTR {
                break;
            }
        }
        res
    }};
}
