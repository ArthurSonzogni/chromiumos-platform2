// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod sys;
pub use crosvm_base::unix::panic_handler;

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
pub mod rand;
pub mod scoped_path;
pub mod secure_blob;
pub mod syslog;
