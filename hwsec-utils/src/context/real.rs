// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Context;
use crate::command_runner::RealCommandRunner;

pub struct RealContext {
    cmd_runner: RealCommandRunner,
}
impl Default for RealContext {
    fn default() -> Self {
        Self::new()
    }
}
impl RealContext {
    pub fn new() -> Self {
        Self {
            cmd_runner: RealCommandRunner,
        }
    }
}

impl Context for RealContext {
    type CommandRunner = RealCommandRunner;
    fn cmd_runner(&mut self) -> &mut Self::CommandRunner {
        &mut self.cmd_runner
    }
}
