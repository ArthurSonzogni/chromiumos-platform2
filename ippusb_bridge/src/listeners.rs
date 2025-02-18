// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::io;
use std::ops::Deref;
use std::os::unix::io::{AsRawFd, RawFd};

use tokio::net::UnixListener;

use log::error;

/// Scopes a UnixListener such that on Drop, the local socket path, if any, is deleted.
pub struct ScopedUnixListener(pub UnixListener);

impl AsRawFd for ScopedUnixListener {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

impl Deref for ScopedUnixListener {
    type Target = UnixListener;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for ScopedUnixListener {
    fn drop(&mut self) {
        if let Ok(sa) = self.0.local_addr() {
            if let Some(path) = sa.as_pathname() {
                if let Err(e) = fs::remove_file(path) {
                    if e.kind() != io::ErrorKind::NotFound {
                        error!("Failed to remove socket at {}: {}", path.display(), e);
                    }
                }
            }
        }
    }
}
