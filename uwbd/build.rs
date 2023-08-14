// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for uwbd.

use chromeos_dbus_bindings::{generate_module, BindingsType, CROSSROADS_SERVER_OPTS};
use std::path::Path;

// The parent path of uwbd.
const SOURCE_DIR: &str = ".";

// (<module name>, <relative path to source xml>)
const BINDINGS_TO_GENERATE: &[(&str, &str, BindingsType)] = &[(
    "org_chromium_uwbd",
    "dbus_bindings/org.chromium.uwbd.xml",
    BindingsType::Both {
        client_opts: None,
        server_opts: CROSSROADS_SERVER_OPTS,
    },
)];

/// Helper class used to collect and provide source file paths.
struct Files {
    files: Vec<String>,
}

impl Files {
    fn new() -> Files {
        Files { files: Vec::new() }
    }

    fn add_subdir<A: std::fmt::Display>(
        &mut self,
        path_prefix: &str,
        file_names: impl IntoIterator<Item = A>,
    ) {
        self.files.extend(
            file_names
                .into_iter()
                .map(|f| format!("{}/{}", path_prefix, f)),
        );
    }

    fn metadata(&self) {
        for f in &self.files {
            println!("cargo:rerun-if-changed={}", f);
        }
    }
}

/// Builds the NXP HAL source code by making use of the CC crate.
fn build_nxp_hal() {
    let mut builder = cc::Build::new();
    let mut files = Files::new();

    files.add_subdir("nxp_hal/halimpl/fwd/sr1xx", ["phNxpUciHal_fwd.cc"]);
    files.add_subdir(
        "nxp_hal/halimpl/hal",
        ["phNxpUciHal_ext.cc", "phNxpUciHal.cc"],
    );
    files.add_subdir("nxp_hal/halimpl/log", ["phNxpLog.cc"]);
    files.add_subdir(
        "nxp_hal/halimpl/tml",
        [
            "phDal4Uwb_messageQueueLib.cc",
            "phOsalUwb_Timer.cc",
            "phTmlUwb_spi.cc",
            "phTmlUwb.cc",
        ],
    );
    files.add_subdir(
        "nxp_hal/halimpl/utils",
        ["phNxpConfig.cc", "phNxpUciHal_utils.cc"],
    );

    files.metadata();

    builder
        .cargo_metadata(true)
        .files(files.files)
        .extra_warnings(false)
        .cpp(true)
        .cpp_set_stdlib("c++")
        .cpp_link_stdlib("c++")
        .flag("-std=c++20")
        .define("OS_CHROMEOS", "None")
        .include("nxp_hal/extns/inc")
        .include("nxp_hal/halimpl/fwd/sr1xx")
        .include("nxp_hal/halimpl/hal")
        .include("nxp_hal/halimpl/inc/common")
        .include("nxp_hal/halimpl/inc")
        .include("nxp_hal/halimpl/log")
        .include("nxp_hal/halimpl/tml")
        .include("nxp_hal/halimpl/utils");

    let lib_dependencies = vec!["libchrome", "libbrillo"];

    for lib_name in lib_dependencies.iter() {
        let lib = pkg_config::probe_library(lib_name).unwrap();

        lib.include_paths.into_iter().for_each(|item| {
            builder.include(&item);
        })
    }

    builder.compile("nxphal");
}

fn main() {
    // Build the NXP middleware/HAL as a static library.
    build_nxp_hal();

    // Generate the D-Bus bindings to "src/bindings/include_modules.rs".
    generate_module(Path::new(SOURCE_DIR), BINDINGS_TO_GENERATE).unwrap();
}
