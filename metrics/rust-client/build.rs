// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::path::PathBuf;

fn main() {
    // Rerun if the c header changes.
    println!("cargo:rerun-if-changed=../c_metrics_library.h");

    // Dynamically link to libmetrics.
    println!("cargo:rustc-link-lib=dylib=metrics");

    // Generate the c binding to c_metrics_library.h.
    let bindings = bindgen::Builder::default()
        .header("c_metrics_library.h")
        .blocklist_function("CMetricsLibraryInit") // CMetricsLibraryInit is deprecated.
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
