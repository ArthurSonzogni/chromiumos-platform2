// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(feature = "vm_grpc")]
use anyhow::Result;
#[cfg(feature = "vm_grpc")]
use std::env;
#[cfg(feature = "vm_grpc")]
use std::fs::{self, File};
#[cfg(feature = "vm_grpc")]
use std::io::Write;
#[cfg(feature = "vm_grpc")]
use std::path::{Path, PathBuf};
#[cfg(feature = "vm_grpc")]
extern crate protoc_grpcio;

// TODO(b:277383885): Share proto building code with crostini_client
#[cfg(feature = "vm_grpc")]
const PROTOS_TO_GENERATE: &[(&str, &str)] = &[
    ("arc", "dbus/arc/arc.proto"),
    ("vm_concierge", "dbus/vm_concierge/concierge_service.proto"),
];

#[cfg(feature = "vm_grpc")]
fn generate_protos(source_dir: &Path, protos: &[(&str, &str)]) -> Result<()> {
    let out_dir = PathBuf::from("src/vm_grpc/vm_grpc_util/");
    if out_dir.exists() {
        // If CROS_RUST is set, skip generation.
        if env::var("CROS_RUST") == Ok(String::from("1")) {
            return Ok(());
        }
        fs::remove_dir_all(&out_dir)?;
    }
    fs::create_dir_all(&out_dir)?;
    let mut out = File::create(out_dir.join("include_protos.rs"))?;
    for (module, input_path) in protos {
        let input_path = source_dir.join(input_path);
        let input_dir = input_path.parent().unwrap();
        let parent_input_dir = source_dir.join("dbus");
        // Invoke protobuf compiler.
        protoc_rust::Codegen::new()
            .input(input_path.as_os_str().to_str().unwrap())
            .include(input_dir.as_os_str().to_str().unwrap())
            .include(parent_input_dir)
            .out_dir(&out_dir)
            .run()
            .expect("protoc could not compile concierge_service proto");

        // Write out a `mod` that refers to the generated module.
        writeln!(out, "pub mod {};", module)?;
    }
    Ok(())
}

fn main() {
    #[cfg(feature = "vm_grpc")]
    {
        println!("Building gRPC autogen code...");
        let proto_root = "src/vm_grpc/proto/";
        println!("cargo:rerun-if-changed={}", proto_root);
        protoc_grpcio::compile_grpc_protos(
            &["resourced_bridge.proto"],
            &[proto_root],
            &proto_root,
            None,
        )
        .expect("Failed to compile gRPC definitions!");
        let source_dir = match env::var("SYSROOT") {
            Ok(path) => PathBuf::from(path).join("usr/include/chromeos"),
            // Make this work when typing "cargo build" in platform2/vm_tools/crostini_client
            Err(_) => PathBuf::from("../system_api"),
        };
        generate_protos(&source_dir, PROTOS_TO_GENERATE).unwrap();
    }
}
