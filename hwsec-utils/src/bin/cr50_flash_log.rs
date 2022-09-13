// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;

use hwsec_utils::context::RealContext;
use hwsec_utils::cr50::cr50_flash_log;

fn main() {
    let mut real_ctx = RealContext::new();
    let args_string: Vec<String> = env::args().collect();
    let args: Vec<&str> = args_string.iter().map(|s| s.as_str()).collect();
    match cr50_flash_log(
        &mut real_ctx,
        args[2]
            .parse::<u32>()
            .expect("The 2nd argument should be a u32"),
        args[3]
            .parse::<u32>()
            .expect("The 3rd argument should be a u32"),
    ) {
        Ok(()) => std::process::exit(0),
        Err(_) => std::process::exit(-1),
    }
}
