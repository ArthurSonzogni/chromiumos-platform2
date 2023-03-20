// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(feature = "vm_grpc")]
extern crate protoc_grpcio;

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
    }
}
