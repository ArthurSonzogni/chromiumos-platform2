# Featured Rust wrapper

Note: This crate is specific to ChromeOS and requires the native
[libfeatures_c](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/featured)
library at link time.

`featured-rs` is a Rust wrapper for libfeatures_c. This library is used to interact with Featured to check
which features are currently enabled.

## Building for the host environment

You can also execute `cargo` directly for faster build and tests. This would be useful when you are
developing this crate.

```shell
# Install libfeatures_c.so to host.
(chroot)$ sudo emerge chromeos-base/featured
# Build
(chroot)$ cargo build
```

## Testing

Testing the package can be done by emerging for a board of your choice with `FEATURES=test` set.

```shell
# Test
(chroot)$ FEATURES="test" emerge-${BOARD} dev-rust/featured
```

## Generated bindings

The build script, `build_buildings.sh`, generates bindings to `../c_feature_library.h`.
Whenever breaking changes are made to `../c_featured_library.h`, this build script must be
rerun to generate new bindings to the C library.
