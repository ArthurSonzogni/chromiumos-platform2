// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;

mod md5;
mod util;
use util::Args;

#[cfg(feature = "vaapi")]
mod vaapi_decoder;
#[cfg(feature = "vaapi")]
use vaapi_decoder::do_decode;

fn main() {
    env_logger::init();

    let args: Args = argh::from_env();

    let input = File::open(&args.input).expect("error opening input file");

    do_decode(input, args);
}
