// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hwsec_utils::context::RealContext;
use hwsec_utils::cr50::cr50_flash_log;
use hwsec_utils::cr50::read_prev_timestamp_from_file;
use hwsec_utils::cr50::set_cr50_log_file_time_base;
use hwsec_utils::cr50::update_timestamp_file;

const TIMESTAMP_FILE: &str = "/mnt/stateful_partition/unencrypted/preserve/cr50_flog_timestamp";
fn main() {
    let mut real_ctx = RealContext::new();

    let Ok(prev_stamp) = read_prev_timestamp_from_file(TIMESTAMP_FILE) else {
        std::process::exit(1)
    };

    if set_cr50_log_file_time_base(&mut real_ctx).is_err() {
        std::process::exit(1)
    };

    let (ret_code, new_stamp): (i32, u64) = match cr50_flash_log(&mut real_ctx, prev_stamp) {
        Ok(new_stamp) => (0, new_stamp),
        Err((_, new_stamp)) => (1, new_stamp),
    };

    if new_stamp > prev_stamp && update_timestamp_file(new_stamp, TIMESTAMP_FILE).is_err() {
        std::process::exit(1)
    }

    std::process::exit(ret_code)
}
