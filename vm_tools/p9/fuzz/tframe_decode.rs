// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![no_main]

extern crate p9;

use std::panic;
use std::process;
use std::slice;

use p9::fuzzing::tframe_decode;

#[export_name="LLVMFuzzerTestOneInput"]
pub fn test_one_input(data: *const u8, size: usize) -> i32 {
    // We cannot unwind past ffi boundaries.
    panic::catch_unwind(|| {
        // Safe because the libfuzzer runtime will guarantee that `data` is at least
        // `size` bytes long and that it will be valid for the lifetime of this
        // function.
        let bytes = unsafe { slice::from_raw_parts(data, size) };

        tframe_decode(bytes);
    }).err().map(|_| process::abort());

    0
}
