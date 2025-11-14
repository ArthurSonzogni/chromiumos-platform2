// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::Path;

use log::info;

use crate::feature;

const INIT_DL_SERVER_FEATURE_NAME: &str = "CrOSLateBootInitDLServer";
const INIT_DL_SERVER_FEATURE_DEFAULT_VALUE: bool = false;
const FAIR_SERVER_PARAMS_PATH: &str = "/sys/kernel/debug/sched/fair_server/params";
const DL_SERVER_RUNTIME: &str = "15000000";
const DL_SERVER_PERIOD: &str = "25000000";
const DL_SERVER_DEFAULT_RUNTIME: &str = "50000000";
const DL_SERVER_DEFAULT_PERIOD: &str = "1000000000";

pub fn register_features() {
    // If DL Server is unavailable, don't register any RT features
    // as we don't want data from devices without it available. RT
    // is dangerous and requires proper throttling.
    if !is_dlserver_available() {
        return;
    }

    // The DL Server feature throttles RT if it is starving
    // CFS while also not wasting cycles on idle CPUs.
    feature::register_feature(
        INIT_DL_SERVER_FEATURE_NAME,
        INIT_DL_SERVER_FEATURE_DEFAULT_VALUE,
        Some(Box::new(move |_| {
            init_dlserver_params();
        })),
    );

    init_dlserver_params();
}

/// Returns whether DL Server is available on the system or not.
pub fn is_dlserver_available() -> bool {
    Path::new(FAIR_SERVER_PARAMS_PATH).exists()
}

fn init_dlserver_params() {
    // If the default feature value is True and if feature is not available
    // yet because we have not called feature_init, we end up setting the
    // non-default parameters. This will be useful when we want to always
    // default to enabling the DL server feature.
    if feature::is_feature_enabled(INIT_DL_SERVER_FEATURE_NAME)
        .unwrap_or(INIT_DL_SERVER_FEATURE_DEFAULT_VALUE)
    {
        // Set 20ms/25ms if default feature value is True and feature is not
        // disabled.
        config_dlserver_params(DL_SERVER_RUNTIME, DL_SERVER_PERIOD);
    } else {
        config_dlserver_params(DL_SERVER_DEFAULT_RUNTIME, DL_SERVER_DEFAULT_PERIOD);
    }
}

fn config_dlserver_params(runtime: &str, period: &str) {
    let params_path = Path::new(FAIR_SERVER_PARAMS_PATH);
    if !params_path.exists() {
        return;
    }
    if let Err(e) = fs::write(params_path, format!("{period},{runtime}\0")) {
        info!(
            "Could not write to to params, {params_path:?}, \
            (period: {period}, runtime: {runtime}) {e:?}"
        );
    }
}
