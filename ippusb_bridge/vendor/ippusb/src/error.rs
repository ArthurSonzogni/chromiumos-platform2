// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io;

#[derive(Debug)]
pub enum Error {
    ClaimInterface(u8, rusb::Error),
    ReleaseInterface(u8, rusb::Error),
    DetachDrivers(u8, rusb::Error),
    AttachDrivers(u8, rusb::Error),
    CleanupThread(io::Error),
    ReadConfigDescriptor(rusb::Error),
    ReadDeviceDescriptor(rusb::Error),
    SetActiveConfig(rusb::Error),
    SetAlternateSetting(u8, rusb::Error),
    NoFreeInterface,
    NotIppUsb,
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            ClaimInterface(i, err) => write!(f, "Failed to claim interface {}: {}", i, err),
            ReleaseInterface(i, err) => write!(f, "Failed to release interface {}: {}", i, err),
            DetachDrivers(i, err) => write!(
                f,
                "Failed to detach kernel driver for interface {}: {}",
                i, err
            ),
            AttachDrivers(i, err) => write!(
                f,
                "Failed to attach kernel driver for interface {}, {}",
                i, err
            ),
            CleanupThread(err) => write!(f, "Failed to start cleanup thread: {}", err),
            ReadConfigDescriptor(err) => write!(f, "Failed to read config descriptor: {}", err),
            ReadDeviceDescriptor(err) => write!(f, "Failed to read device descriptor: {}", err),
            SetActiveConfig(err) => write!(f, "Failed to set active config: {}", err),
            SetAlternateSetting(i, err) => write!(
                f,
                "Failed to set interface {} alternate setting: {}",
                i, err
            ),
            NoFreeInterface => write!(f, "There is no free IPP USB interface to claim."),
            NotIppUsb => write!(f, "The specified device is not an IPP USB device."),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;
