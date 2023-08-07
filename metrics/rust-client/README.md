# Metrics Rust wrapper

Note: This crate is specific to ChromeOS and requires the native
[libmetrics](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/metrics)
library at link time.

`metrics_rs` is a Rust wrapper for libmetrics. This library is used to upload metrics to the UMA server.


## Running the example on DUT

Building and uploading the example.

```shell
(chroot)$ cargo build --release --examples
(chroot)$ scp target/release/examples/metrics $DUT:/usr/local/bin
```

Running the example on your DUT:

```shell
(DUT)$ metrics
```
