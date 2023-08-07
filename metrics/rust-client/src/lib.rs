// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

use std::io::{Error, ErrorKind};

use crate::bindings::*;

pub struct MetricsLibrary {
    handle: CMetricsLibrary,
}

impl MetricsLibrary {
    pub fn new() -> Result<Self, Error> {
        // Safety: Calls a C function.
        let handle = unsafe { CMetricsLibraryNew() };
        if handle.is_null() {
            return Err(Error::new(ErrorKind::Other, "CMetricsLibraryNew failed"));
        }
        Ok(MetricsLibrary { handle })
    }

    pub fn send_to_uma(
        &mut self,
        name: &str,
        sample: i32,
        min: i32,
        max: i32,
        nbuckets: i32,
    ) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result = unsafe {
            CMetricsLibrarySendToUMA(self.handle, c_name.as_ptr(), sample, min, max, nbuckets)
        };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_enum_to_uma(&mut self, name: &str, sample: i32, max: i32) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result =
            unsafe { CMetricsLibrarySendEnumToUMA(self.handle, c_name.as_ptr(), sample, max) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendEnumToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_repeated_enum_to_uma(
        &mut self,
        name: &str,
        sample: i32,
        max: i32,
        num_samples: i32,
    ) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result = unsafe {
            CMetricsLibrarySendRepeatedEnumToUMA(
                self.handle,
                c_name.as_ptr(),
                sample,
                max,
                num_samples,
            )
        };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendRepeatedEnumToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_linear_to_uma(&mut self, name: &str, sample: i32, max: i32) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result =
            unsafe { CMetricsLibrarySendLinearToUMA(self.handle, c_name.as_ptr(), sample, max) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendLinearToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_percentage_to_uma(&mut self, name: &str, sample: i32) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result =
            unsafe { CMetricsLibrarySendPercentageToUMA(self.handle, c_name.as_ptr(), sample) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendPercentageToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_sparse_to_uma(&mut self, name: &str, sample: i32) -> Result<(), Error> {
        let c_name = std::ffi::CString::new(name)?;
        // Safety: Calls a C function. The argument types are checked.
        let result =
            unsafe { CMetricsLibrarySendSparseToUMA(self.handle, c_name.as_ptr(), sample) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendSparseToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_user_action_to_uma(&mut self, action: &str) -> Result<(), Error> {
        let c_action = std::ffi::CString::new(action)?;
        // Safety: Calls a C function. The argument types are checked.
        let result = unsafe { CMetricsLibrarySendUserActionToUMA(self.handle, c_action.as_ptr()) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendUserActionToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_crash_to_uma(&mut self, crash_kind: &str) -> Result<(), Error> {
        let c_crash_kind = std::ffi::CString::new(crash_kind)?;
        // Safety: Calls a C function. The argument types are checked.
        let result = unsafe { CMetricsLibrarySendCrashToUMA(self.handle, c_crash_kind.as_ptr()) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendCrashToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn send_cros_event_to_uma(&mut self, event: &str) -> Result<(), Error> {
        let c_event = std::ffi::CString::new(event)?;
        // Safety: Calls a C function. The argument types are checked.
        let result = unsafe { CMetricsLibrarySendCrosEventToUMA(self.handle, c_event.as_ptr()) };
        if result == 0 {
            return Err(Error::new(
                ErrorKind::Other,
                "CMetricsLibrarySendCrosEventToUMA failed",
            ));
        }
        Ok(())
    }

    pub fn are_metrics_enabled(&mut self) -> bool {
        // Safety: Calls a C function. The argument type is checked.
        (unsafe { CMetricsLibraryAreMetricsEnabled(self.handle) }) != 0
    }
}

impl Drop for MetricsLibrary {
    fn drop(&mut self) {
        // Safety: Calls a C function. The argument type is checked.
        unsafe { CMetricsLibraryDelete(self.handle) }
    }
}
