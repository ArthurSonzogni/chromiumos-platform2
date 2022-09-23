// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::fmt::Display;

#[derive(Debug, PartialEq)]
pub enum HwsecError {
    InvalidArgumentError,
    Tpm2Error(u32),
    Tpm2ResponseBadFormatError,
    GsctoolError(i32),
    GsctoolResponseBadFormatError,
    MetricsClientFailureError(String),
    CommandRunnerError,
    SyslogError,
    FileError,
    SystemTimeError,
    InternalError,
}

impl Display for HwsecError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            HwsecError::InvalidArgumentError => write!(f, "InvalidArgumentError"),
            HwsecError::Tpm2Error(err_code) => write!(f, "Tpm2Error - Error code: {}", err_code),
            HwsecError::Tpm2ResponseBadFormatError => write!(f, "Tpm2ResponseBadFormatError"),
            HwsecError::GsctoolError(err_code) => {
                write!(f, "GsctoolError - Error code : {}", err_code)
            }
            HwsecError::GsctoolResponseBadFormatError => write!(f, "GsctoolResponseBadFormatError"),
            HwsecError::MetricsClientFailureError(err_msg) => {
                write!(f, "MetricsClientFailureError: {}", err_msg)
            }
            HwsecError::CommandRunnerError => write!(f, "CommandRunnerError"),
            HwsecError::SyslogError => write!(f, "SyslogError"),
            HwsecError::FileError => write!(f, "FileError"),
            HwsecError::SystemTimeError => write!(f, "SystemTimeError"),
            HwsecError::InternalError => write!(f, "InternalError"),
        }
    }
}
