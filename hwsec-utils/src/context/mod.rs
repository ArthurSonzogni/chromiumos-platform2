// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::command_runner::CommandRunner;

mod real;
pub use real::*;

#[cfg(test)]
pub(crate) mod mock;

pub trait Context {
    type CommandRunner: CommandRunner;
    fn cmd_runner(&mut self) -> &mut Self::CommandRunner;
}
