// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bindings;
use crate::bindings::*;

pub trait CheckFeature {
    fn is_feature_enabled_blocking(&self, feature: &Feature) -> bool;
}

#[derive(Debug)]
pub struct Feature<'a> {
    name: &'a str,
    c_feature: VariationsFeature,
}

impl<'a> Feature<'a> {
    pub fn new(name: &'a str, enabled_by_default: bool) -> Result<Self, &str> {
        // We are allocating a string and transferring ownership to the C library,
        // which prevents the underlying memory from being deallocated. We must
        // manually retake ownership via the Drop implementation below to properly
        // prevent memory leaks.
        // See: https://doc.rust-lang.org/std/ffi/struct.CString.html#method.into_raw
        let c_name = std::ffi::CString::new(name)
            .map_err(|_| "Feature names must not contain \\0")?
            .into_raw();
        let default_state = if enabled_by_default {
            FeatureState_FEATURE_ENABLED_BY_DEFAULT
        } else {
            FeatureState_FEATURE_DISABLED_BY_DEFAULT
        };
        let c_feature = VariationsFeature {
            name: c_name,
            default_state,
        };
        Ok(Feature { name, c_feature })
    }

    pub fn name(&self) -> &str {
        self.name
    }

    pub fn enabled_by_default(&self) -> bool {
        self.c_feature.default_state == FeatureState_FEATURE_ENABLED_BY_DEFAULT
    }
}

impl<'a> Drop for Feature<'a> {
    fn drop(&mut self) {
        // We need to take back ownership of the string we created originally
        // to that it will deallocate memory properly.
        // See: https://doc.rust-lang.org/std/ffi/struct.CString.html#method.from_raw
        // SAFETY: The raw pointer here is guaranteed to be valid since it was created
        // when this struct was instantiated and the C library does not mutate the
        // feature object when checking for feature enablement.
        let _ = unsafe {
            std::ffi::CString::from_raw(self.c_feature.name as *mut ::std::os::raw::c_char)
        };
    }
}

pub struct PlatformFeatures {
    handle: CFeatureLibrary,
}

impl PlatformFeatures {
    pub fn new() -> Result<Self, &'static str> {
        // SAFETY: The C library can return either a valid object pointer or a null pointer.
        // A subsequent check is made to ensure that the pointer is valid.
        let handle = unsafe { CFeatureLibraryNew() };
        if handle.is_null() {
            return Err("Unable to instantiate CFeatureLibrary.");
        };
        Ok(PlatformFeatures { handle })
    }
}

impl Drop for PlatformFeatures {
    fn drop(&mut self) {
        // SAFETY: The C call frees the memory associated with handle, which is okay since the
        // pointer is dropped immediately afterwards.
        unsafe { CFeatureLibraryDelete(self.handle) }
    }
}

impl CheckFeature for PlatformFeatures {
    fn is_feature_enabled_blocking(&self, feature: &Feature) -> bool {
        // SAFETY: The C call is guaranteed to return a valid value and does not modify
        // the underlying handle or feature.
        let result = unsafe { CFeatureLibraryIsEnabledBlocking(self.handle, &feature.c_feature) };
        result != 0
    }
}

pub struct FakePlatformFeatures {
    handle: CFeatureLibrary,
}

impl FakePlatformFeatures {
    pub fn new() -> Result<Self, &'static str> {
        // SAFETY: The C library can return either a valid object pointer or a null pointer.
        // A subsequent check is made to ensure that the pointer is valid.
        let handle = unsafe { FakeCFeatureLibraryNew() };
        if handle.is_null() {
            return Err("Unable to instantiate CFeatureLibrary.");
        };
        Ok(FakePlatformFeatures { handle })
    }

    pub fn set_feature_enabled(&mut self, feature: &Feature, is_enabled: bool) {
        // SAFETY: The C call will never invalidate the handle or feature pointers.
        unsafe {
            FakeCFeatureLibrarySetEnabled(self.handle, feature.c_feature.name, is_enabled.into())
        }
    }

    pub fn clear_feature_enabled(&mut self, feature: &Feature) {
        // SAFETY: The C call will never invalidate the handle or feature pointers.
        unsafe { FakeCFeatureLibraryClearEnabled(self.handle, feature.c_feature.name) }
    }
}

impl Drop for FakePlatformFeatures {
    fn drop(&mut self) {
        // SAFETY: The C call frees the memory associated with handle, which is okay since the
        // pointer is dropped immediately afterwards.
        unsafe { CFeatureLibraryDelete(self.handle) }
    }
}

impl CheckFeature for FakePlatformFeatures {
    fn is_feature_enabled_blocking(&self, feature: &Feature) -> bool {
        // SAFETY: The C call is guaranteed to return a valid value and does not modify
        // the underlying handle or feature.
        let result = unsafe { CFeatureLibraryIsEnabledBlocking(self.handle, &(feature.c_feature)) };
        result != 0
    }
}

#[test]
fn it_can_create_a_valid_feature() {
    let subject = Feature::new("some-valid-feature", true);
    assert!(subject.is_ok());
}

#[test]
fn it_rejects_an_invalid_feature() {
    let subject = Feature::new("some-bad-feature\0", true);
    assert!(subject.is_err());
}

#[test]
fn it_properly_fakes_the_feature_library() {
    let mut subject = FakePlatformFeatures::new().unwrap();
    let feature_one = Feature::new("some-valid-feature", false).unwrap();
    let feature_two = Feature::new("other-valid-feature", true).unwrap();

    assert!(!subject.is_feature_enabled_blocking(&feature_one));
    assert!(subject.is_feature_enabled_blocking(&feature_two));

    subject.set_feature_enabled(&feature_one, true);
    subject.set_feature_enabled(&feature_two, false);

    assert!(subject.is_feature_enabled_blocking(&feature_one));
    assert!(!subject.is_feature_enabled_blocking(&feature_two));

    subject.clear_feature_enabled(&feature_one);
    subject.clear_feature_enabled(&feature_two);

    assert!(!subject.is_feature_enabled_blocking(&feature_one));
    assert!(subject.is_feature_enabled_blocking(&feature_two));
}
