// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ties together the various modules that make up the Sirenia library used by
//! both Trichechus and Dugong.

#![deny(unsafe_op_in_unsafe_fn)]

pub mod app_info;
pub mod secrets;

include!("bindings/include_modules.rs");

use libsirenia::communication::Digest;
use openssl::{
    error::ErrorStack,
    hash::{hash, MessageDigest},
};

pub fn compute_sha256(data: &[u8]) -> Result<Digest, ErrorStack> {
    hash(MessageDigest::sha256(), data).map(From::from)
}
