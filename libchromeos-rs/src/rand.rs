// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust implementation of functionality parallel to libchrome's base/rand_util.h.

use std::{thread::sleep, time::Duration};

use libc::{c_uint, c_void};
use nix::Error;
use nix::Result;

use crate::handle_eintr_errno;

/// How long to wait before calling getrandom again if it does not return
/// enough bytes.
const POLL_INTERVAL: Duration = Duration::from_millis(50);

/// Represents whether or not the random bytes are pulled from the source of
/// /dev/random or /dev/urandom.
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub enum Source {
    // This is the default and uses the same source as /dev/urandom.
    #[default]
    Pseudorandom,
    // This uses the same source as /dev/random and may be.
    Random,
}

impl Source {
    fn to_getrandom_flags(&self) -> c_uint {
        match self {
            Source::Random => libc::GRND_RANDOM,
            Source::Pseudorandom => 0,
        }
    }
}

/// Fills `output` completely with random bytes from the specified `source`.
pub fn rand_bytes(mut output: &mut [u8], source: Source) -> Result<()> {
    if output.is_empty() {
        return Ok(());
    }

    loop {
        // Safe because output is mutable and the writes are limited by output.len().
        let bytes = handle_eintr_errno!(unsafe {
            libc::getrandom(
                output.as_mut_ptr() as *mut c_void,
                output.len(),
                source.to_getrandom_flags(),
            )
        });

        if bytes < 0 {
            return Err(Error::last());
        }
        if bytes as usize == output.len() {
            return Ok(());
        }

        // Wait for more entropy and try again for the remaining bytes.
        sleep(POLL_INTERVAL);
        output = &mut output[bytes as usize..];
    }
}

/// Allocates a vector of length `len` filled with random bytes from the
/// specified `source`.
pub fn rand_vec(len: usize, source: Source) -> Result<Vec<u8>> {
    let mut rand = Vec::with_capacity(len);
    if len == 0 {
        return Ok(rand);
    }

    // Safe because rand will either be initialized by getrandom or dropped.
    unsafe { rand.set_len(len) };
    rand_bytes(rand.as_mut_slice(), source)?;
    Ok(rand)
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_SIZE: usize = 64;

    #[test]
    fn randbytes_success() {
        let mut rand = vec![0u8; TEST_SIZE];
        rand_bytes(&mut rand, Source::Pseudorandom).unwrap();
        assert_ne!(&rand, &[0u8; TEST_SIZE]);
    }

    #[test]
    fn randvec_success() {
        let rand = rand_vec(TEST_SIZE, Source::Pseudorandom).unwrap();
        assert_eq!(rand.len(), TEST_SIZE);
        assert_ne!(&rand, &[0u8; TEST_SIZE]);
    }

    #[test]
    fn sourcerandom_success() {
        let rand = rand_vec(TEST_SIZE, Source::Random).unwrap();
        assert_ne!(&rand, &[0u8; TEST_SIZE]);
    }
}
