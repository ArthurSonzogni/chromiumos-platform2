 # resourced::VM_GRPC - GRPC support for select ChromeOS boards

This module adds select GRPC API support to resourced.  The GRPCs are used to call the guest VM over VSOCK on projects: hades/draco/agah. For detailed design of the GRPC's, visit internal site: [go/resourced-grpcs](http://go/resourced-grpcs)

## Usage

VM_GRPC is disabled on resourced build by default.  To enable it, build resourced with the feature enabled:
```bash
cargo build --features vm_grpc
```

The standard `grpcio` rust crate doesn't not support GRPC over vsock.  For enabling vsock support, this [patch](https://chromium-review.googlesource.com/c/chromiumos/overlays/chromiumos-overlay/+/3966266/3/dev-rust/grpcio-sys/files/002-grpc-vsock-support.patch) has to be applied to the `grpcio-sys`.

# Proto
The proto directory contains `resource_bridge.proto`, which defines all the interfaces and GRPC API calls supported by the server.  It also has definitions of client calls the host side resourced can make.  The other files in the directory are auto generated, can can be created using the `protoc_grpcio` crate and a `build.rs` script that contains:
```rust
let proto_root = "src/vm_bridge/proto/";
protoc_grpcio::compile_grpc_protos(
        &["resourced_bridge.proto"],
        &[proto_root],
        &proto_root,
        None,
    )
```

# Known Issues

* ebuild support is for this feature is pending.
* `grpcio-sys` vsock is manually patched (instructions above), to be integrated into build system.
* `grpcio-sys` uses `libstdc++.so.6`, which isn't available on ChromeOS.  Can be bypassed by copying over `libstdc++.so.6` from build system to `$DUT://usr/lib64/`
