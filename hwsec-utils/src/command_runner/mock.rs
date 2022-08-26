// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;
use std::os::unix::prelude::ExitStatusExt;
use std::process::ExitStatus;
use std::process::Output;

use crate::command_runner::CommandRunner;
use crate::cr50::GSCTOOL_CMD_NAME;

// For any member variable x in MockCommandInput:
// x = Some(_) means that we would check the correspondence;
// otherwise, we don't really care about its exact value.
// To know more, take a glance at impl CommandRunner for MockCommandRunner.
pub struct MockCommandInput {
    pub cmd_name: Option<String>,
    pub args: Option<Vec<String>>,
}

impl MockCommandInput {
    fn new(cmd_name: &str, args: Vec<&str>) -> Self {
        Self {
            cmd_name: Some(cmd_name.to_owned()),
            args: Some(args.iter().map(|&s| s.into()).collect()),
        }
    }
}

pub struct MockCommandOutput {
    pub result: Result<Output, std::io::Error>,
}

impl MockCommandOutput {
    fn new(exit_status: i32, out: &str, err: &str) -> Self {
        Self {
            result: Ok(Output {
                status: ExitStatus::from_raw(exit_status),
                stdout: out.to_owned().as_bytes().to_vec(),
                stderr: err.to_owned().as_bytes().to_vec(),
            }),
        }
    }
}

pub struct MockCommandRunner {
    expectations: VecDeque<(MockCommandInput, MockCommandOutput)>,
}

impl Default for MockCommandRunner {
    fn default() -> Self {
        Self::new()
    }
}

impl MockCommandRunner {
    pub fn new() -> Self {
        MockCommandRunner {
            expectations: VecDeque::new(),
        }
    }
    pub fn add_expectation(&mut self, inp: MockCommandInput, out: MockCommandOutput) {
        self.expectations.push_back((inp, out));
    }
    pub fn set_trunksd_running(&mut self, status: bool) {
        self.add_expectation(
            MockCommandInput::new("status", vec!["trunksd"]),
            MockCommandOutput::new(
                0,
                if status {
                    "trunksd start/running, process 17302"
                } else {
                    "trunksd stop/waiting"
                },
                "",
            ),
        );
    }
    pub fn add_tpm_interaction(
        &mut self,
        cmd_name: &str,
        flag: Vec<&str>,
        hex_str_tokens: Vec<&str>,
        exit_status: i32,
        out: &str,
        err: &str,
    ) {
        self.add_expectation(
            MockCommandInput::new(cmd_name, [&flag[..], &hex_str_tokens[..]].concat()),
            MockCommandOutput::new(exit_status, out, err),
        );
    }
    pub fn add_gsctool_interaction(
        &mut self,
        flag: Vec<&str>,
        exit_status: i32,
        out: &str,
        err: &str,
    ) {
        self.add_expectation(
            MockCommandInput::new(GSCTOOL_CMD_NAME, flag),
            MockCommandOutput::new(exit_status, out, err),
        );
    }
}

impl CommandRunner for MockCommandRunner {
    fn run(&mut self, cmd_name: &str, args: Vec<&str>) -> Result<Output, std::io::Error> {
        assert!(
            !self.expectations.is_empty(),
            "Failed to pop front from queue -- it's empty!"
        );
        let io_pair = self.expectations.pop_front().unwrap();
        let inp = io_pair.0;
        let out = io_pair.1;
        if let Some(inp_name) = inp.cmd_name {
            assert_eq!(cmd_name, inp_name);
        }
        if let Some(inp_args) = inp.args {
            assert_eq!(args, inp_args);
        }
        out.result
    }
}

impl Drop for MockCommandRunner {
    fn drop(&mut self) {
        assert!(self.expectations.is_empty());
    }
}
