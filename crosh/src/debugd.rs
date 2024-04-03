// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provide debugd APIs.  This module should only contain straight translations
// for the org.chromium.debugd interface.  Higher level business logic should
// remain in the various command modules.

use bitflags::bitflags;
use dbus::{arg::PropMap, blocking::Connection};
use log::error;
use system_api::client::OrgChromiumDebugd;

use crate::util::DEFAULT_DBUS_TIMEOUT;

const BUS_NAME: &str = "org.chromium.debugd";
const SERVICE_PATH: &str = "/org/chromium/debugd";

pub struct Debugd {
    connection: dbus::blocking::Connection,
}

// These bitflag values must match those in org.chromium.debugd.xml.
bitflags! {
    pub struct DRMTraceCategories: u32 {
        const CORE =    0x001;
        const DRIVER =  0x002;
        const KMS =     0x004;
        const PRIME =   0x008;
        const ATOMIC =  0x010;
        const VBL =     0x020;
        const STATE =   0x040;
        const LEASE =   0x080;
        const DP =      0x100;
        const DRMRES =  0x200;
    }
}

// These enum values must match those in org.chromium.debugd.xml.
pub enum DRMTraceSize {
    Default = 0,
    Debug = 1,
}

// These enum values must match those in org.chromium.debugd.xml.
pub enum DRMTraceSnapshotType {
    Trace = 0,
    Modetest = 1,
}

impl Debugd {
    pub fn new() -> Result<Debugd, dbus::Error> {
        match Connection::new_system() {
            Ok(connection) => Ok(Debugd { connection }),
            Err(err) => {
                // Include output in syslog.
                error!("ERROR: Failed to get D-Bus connection: {}", err);
                Err(err)
            }
        }
    }

    pub fn call_dmesg(&self, options: PropMap) -> Result<String, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .call_dmesg(options)
    }

    pub fn drmtrace_annotate_log(&self, log: String) -> Result<&Debugd, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .drmtrace_annotate_log(&log)
            .map(|_| self)
    }

    pub fn drmtrace_snapshot(
        &self,
        snapshot_type: DRMTraceSnapshotType,
    ) -> Result<&Debugd, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .drmtrace_snapshot(snapshot_type as u32)
            .map(|_| self)
    }

    pub fn drmtrace_set_size(&self, size: DRMTraceSize) -> Result<&Debugd, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .drmtrace_set_size(size as u32)
            .map(|_| self)
    }

    pub fn drmtrace_set_categories(
        &self,
        categories: DRMTraceCategories,
    ) -> Result<&Debugd, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .drmtrace_set_categories(categories.bits())
            .map(|_| self)
    }

    pub fn get_u2f_flags(&self) -> Result<String, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .get_u2f_flags()
    }

    pub fn set_u2f_flags(&self, flags: &str) -> Result<String, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .set_u2f_flags(flags)
    }

    pub fn upload_crashes(&self) -> Result<&Debugd, dbus::Error> {
        self.connection
            .with_proxy(BUS_NAME, SERVICE_PATH, DEFAULT_DBUS_TIMEOUT)
            .upload_crashes()
            .map(|_| self)
    }
}
