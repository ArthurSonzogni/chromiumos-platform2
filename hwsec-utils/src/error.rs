// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::fmt::Display;

#[derive(Debug, PartialEq)]
pub enum HwsecError {
    InvalidArgumentError,
    Tpm2Error(u32),
    Tpm2ResponseBadFormatError,
    CommandRunnerError,
    InternalError,
}

impl Display for HwsecError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            HwsecError::InvalidArgumentError => write!(f, "InvalidArgumentError"),
            HwsecError::Tpm2Error(err_code) => write!(f, "Tpm2Error - Error code: {}", err_code),
            HwsecError::Tpm2ResponseBadFormatError => write!(f, "Tpm2ResponseBadFormatError"),
            HwsecError::CommandRunnerError => write!(f, "CommandRunnerError"),
            HwsecError::InternalError => write!(f, "InternalError"),
        }
    }
}
