// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for uwbd, builds the nxp hal source
// to a static archive and generates the FFI bindings to it as well.

use chromeos_dbus_bindings::{generate_module, BindingsType, CROSSROADS_SERVER_OPTS};
use std::env;
use std::path::Path;
use std::path::PathBuf;

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
        .define("OS_CHROMEOS", "1")
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

    builder
        .try_flags_from_environment("CPPFLAGS")
        .expect("CPPFLAGS must be specified and UTF-8")
        .compile("nxphal");
}

/// Generates the FFI bindings to the nxphal lib using Bindgen
fn generate_bindings_to_libnxphal() {
    let target_dir = std::env::var_os("OUT_DIR").unwrap();

    println!("cargo:rustc-link-lib=static=nxphal");
    println!(
        "cargo:rustc-link-search=native={}",
        target_dir.clone().into_string().unwrap()
    );

    let clang_args: Vec<&str> = vec!["-x", "c++", "-std=c++17"];

    let libnxphal_include_paths: Vec<&str> = vec!["-Inxp_hal/extns/inc", "-Inxp_hal/halimpl/inc"];

    // Use the bingen::Builder to build up the options to generate
    // the required bindings.
    let bindings = bindgen::Builder::default()
        .clang_args(libnxphal_include_paths)
        .clang_args(clang_args)
        .header("hal_bindings/wrapper.h")
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

fn main() {
    // Build the NXP middleware/HAL as a static library.
    build_nxp_hal();

    // Generate FFI bindings to libnxphal
    generate_bindings_to_libnxphal();

    // Generate the D-Bus bindings to "src/bindings/include_modules.rs".
    generate_module(Path::new(SOURCE_DIR), BINDINGS_TO_GENERATE).unwrap();
}
