// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hwsec_utils::context::RealContext;
use hwsec_utils::cr50::cr50_update;
use hwsec_utils::error::HwsecError;

fn main() {
    let mut real_ctx = RealContext::new();
    match cr50_update(&mut real_ctx) {
        Ok(()) => std::process::exit(0),
        Err(hwsec_error) => match hwsec_error {
            HwsecError::GsctoolError(err_code) => std::process::exit(err_code),
            _ => std::process::exit(-1),
        },
    }
}
