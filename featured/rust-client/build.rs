// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    // Dynamically link to libfeatures_c.
    println!("cargo:rustc-link-lib=dylib=features_c");

    // This c_fake_feature_library is a library for a test,
    // and this rpath and link search directory is required to resolve the library.
    // details: b/242920490
    if cfg!(feature = "fake_backend") {
        if let Ok(sysroot) = std::env::var("SYSROOT") {
            // '/build/lib64' is required to link to the c_fake_feature_library.
            // this rpath is the same as the one that gets added in BUILD.gn
            // if you include the '//common-mk:test' config.
            println!("cargo:rustc-link-arg=-Wl,-rpath=\
            $ORIGIN/:$ORIGIN/lib:/build/lib:/build/lib64:/usr/local/lib:/usr/local/lib64");
            println!("cargo:rustc-link-search=native={}/build/lib64", sysroot);
            println!("cargo:rustc-link-lib=dylib=c_fake_feature_library");
        }
    }
}
