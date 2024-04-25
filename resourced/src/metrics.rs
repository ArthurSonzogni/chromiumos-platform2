// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module is a wrapper of lib metrics_rs to only use it on ebuild.

#[cfg(feature = "chromeos")]
use anyhow::Context;
use anyhow::Result;

// Use metrics_rs on ebuild.
#[cfg(feature = "chromeos")]
pub fn send_to_uma(name: &str, sample: i32, min: i32, max: i32, nbuckets: i32) -> Result<()> {
    use crate::sync::NoPoison;

    let metrics = metrics_rs::MetricsLibrary::get().context("MetricsLibrary::get() failed")?;

    // Shall panic on poisoned mutex.
    metrics
        .do_lock()
        .send_to_uma(name, sample, min, max, nbuckets)?;
    Ok(())
}

// send_to_uma is no-op on cargo build.
#[cfg(not(feature = "chromeos"))]
pub fn send_to_uma(_name: &str, _sample: i32, _min: i32, _max: i32, _nbuckets: i32) -> Result<()> {
    Ok(())
}
