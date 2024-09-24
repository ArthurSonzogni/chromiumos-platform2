// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hwsec_utils::context::RealContext;
use hwsec_utils::gsc::gsc_get_names;

fn main() {
    let mut real_ctx = RealContext::new();
    match gsc_get_names(&mut real_ctx, &["--any"]) {
        Ok(images) => {
            for image in images {
                println!("/{}", image.display());
            }
        }
        Err(_) => {
            eprintln!("Failed to find out which Cr50 imaged should be used")
        }
    }
}
