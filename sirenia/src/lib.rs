// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ties together the various modules that make up the Sirenia library used by
//! both Trichechus and Dugong.

pub mod app_info;
pub mod secrets;

include!("bindings/include_modules.rs");

use std::hash::Hash;
use std::ops::Deref;

use openssl::{
    error::ErrorStack,
    hash::{hash, MessageDigest},
};
use serde::{Deserialize, Serialize};

pub const SHA256_SIZE: usize = 32;

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash, Serialize, Deserialize)]
pub struct Digest([u8; SHA256_SIZE]);

impl Deref for Digest {
    type Target = [u8; SHA256_SIZE];

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<A: AsRef<[u8]>> From<A> for Digest {
    fn from(d: A) -> Self {
        let mut array = [0u8; SHA256_SIZE];
        array.copy_from_slice(d.as_ref());
        Digest(array)
    }
}

pub fn compute_sha256(data: &[u8]) -> Result<Digest, ErrorStack> {
    hash(MessageDigest::sha256(), data).map(From::from)
}
