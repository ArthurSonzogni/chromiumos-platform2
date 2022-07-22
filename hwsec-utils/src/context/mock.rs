// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Context;
use crate::command_runner::MockCommandRunner;

pub(crate) struct MockContext {
    cmd_runner: MockCommandRunner,
}

impl MockContext {
    pub fn new() -> Self {
        Self {
            cmd_runner: MockCommandRunner::new(),
        }
    }
}

impl Context for MockContext {
    type CommandRunner = MockCommandRunner;
    fn cmd_runner(&mut self) -> &mut Self::CommandRunner {
        &mut self.cmd_runner
    }
}
