// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides type safe interfaces for each operation exposed by Chrome's
//! VideoDecodeAccelerator.

use crate::bindings;

use std::slice;

use crate::error::*;
use crate::format::*;

/// Represents a backend implementation of libvda.
pub enum VdaImplType {
    Fake,
    Gavda, // GpuArcVideoDecodeAccelerator
}

/// Represents decoding capabilities of libvda instances.
pub struct Capabilities {
    pub input_formats: Vec<InputFormat>,
    pub output_formats: Vec<PixelFormat>,
}

/// Represents a libvda instance.
pub struct VdaInstance {
    raw_ptr: *mut ::std::os::raw::c_void,
    caps: Capabilities,
}

impl VdaInstance {
    // The callers must garantee that `ptr` is valid for |`num`| elements when both `ptr` and `num`
    // are valid.
    unsafe fn validate_formats<T, U, F>(ptr: *const T, num: usize, f: F) -> VdaResult<Vec<U>>
    where
        F: FnMut(&T) -> VdaResult<U>,
    {
        if num == 0 {
            return Err(VdaError::InvalidCapabilities(
                "num must not be 0".to_string(),
            ));
        }
        if ptr.is_null() {
            return Err(VdaError::InvalidCapabilities(
                "input_formats must not be NULL".to_string(),
            ));
        }

        slice::from_raw_parts(ptr, num)
            .iter()
            .map(f)
            .collect::<VdaResult<Vec<_>>>()
    }

    /// Creates VdaInstance. `typ` specifies which backend will be used.
    pub fn new(typ: VdaImplType) -> VdaResult<Self> {
        let impl_type = match typ {
            VdaImplType::Fake => bindings::vda_impl_type_FAKE,
            VdaImplType::Gavda => bindings::vda_impl_type_GAVDA,
        };

        // Safe because libvda's API is called properly.
        let raw_ptr = unsafe { bindings::initialize(impl_type) };
        if raw_ptr.is_null() {
            return Err(VdaError::InstanceInitFailure);
        }

        // Get available input/output formats.
        // Safe because libvda's API is called properly.
        let vda_cap_ptr = unsafe { bindings::get_vda_capabilities(raw_ptr) };
        if vda_cap_ptr.is_null() {
            return Err(VdaError::GetCapabilitiesFailure);
        }
        // Safe because `vda_cap_ptr` is not NULL.
        let vda_cap = unsafe { *vda_cap_ptr };

        // Safe because `input_formats` is valid for |`num_input_formats`| elements if both are valid.
        let input_formats = unsafe {
            Self::validate_formats(
                vda_cap.input_formats,
                vda_cap.num_input_formats,
                InputFormat::new,
            )?
        };

        // Output formats
        // Safe because `output_formats` is valid for |`num_output_formats`| elements if both are valid.
        let output_formats = unsafe {
            Self::validate_formats(vda_cap.output_formats, vda_cap.num_output_formats, |f| {
                PixelFormat::new(*f)
            })?
        };

        Ok(VdaInstance {
            raw_ptr,
            caps: Capabilities {
                input_formats,
                output_formats,
            },
        })
    }

    /// Get media capabilities.
    pub fn get_capabilities(&self) -> &Capabilities {
        &self.caps
    }
}

impl Drop for VdaInstance {
    fn drop(&mut self) {
        // Safe because libvda's API is called properly.
        unsafe { bindings::deinitialize(self.raw_ptr) }
    }
}
