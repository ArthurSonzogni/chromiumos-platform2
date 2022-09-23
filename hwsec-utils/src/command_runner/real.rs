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
    fn output(&mut self, cmd_name: &str, args: Vec<&str>) -> Result<String, std::io::Error> {
        let run_result = self.run(cmd_name, args)?;
        Ok(String::from_utf8_lossy(&run_result.stdout).to_string())
    }
    fn full_output(&mut self, cmd_name: &str, args: Vec<&str>) -> Result<String, std::io::Error> {
        let run_result = self.run(cmd_name, args)?;
        Ok(String::from_utf8_lossy(&[run_result.stdout, run_result.stderr].concat()).to_string())
    }
}
