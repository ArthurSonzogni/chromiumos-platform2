// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::process::Command;
use std::process::Output;

use super::CommandRunner;
pub struct RealCommandRunner;

impl CommandRunner for RealCommandRunner {
    fn run(&mut self, cmd_name: &str, args: Vec<&str>) -> Result<Output, std::io::Error> {
        Command::new(cmd_name).args(args).output()
    }
}
