// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bindgen::Builder;
use std::path::PathBuf;

const HEADER_PATH: &str = "../c_feature_library.h";

fn main() {
    // Create bindings based on C wrapper for featured.
    let out_dir =
        PathBuf::from(std::env::var("OUT_DIR").expect("Environment variable OUT_DIR undefined"));
    Builder::default()
        .header(HEADER_PATH)
        // May need to point clang to the parent directory to properly find included headers.
        .clang_arg("-I../..")
        .generate()
        .expect("Failed to generate bindings for c_feature_library.h")
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("Failed to write bindings.rs");

    // Only rebuild the bindings if the header file changes.
    println!("cargo:rerun-if-changed={}", HEADER_PATH);

    // Dynamically link to libfeatures_c.
    println!("cargo:rustc-link-lib=dylib=features_c");
}
