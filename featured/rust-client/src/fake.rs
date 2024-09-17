// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bindings::*;

use crate::SafeHandle;
use crate::Feature;
use crate::PlatformError;
use crate::CheckFeature;
use crate::GetParamsAndEnabledResponse;

use std::collections::HashMap;

impl SafeHandle {
    /// Creates a new client handle for faking requests to featured.
    ///
    /// # Errors
    ///
    /// If the underlying C calls do not return a proper handle to
    /// the featured client, an error will be returned.
    fn fake() -> Result<Self, PlatformError> {
        // SAFETY: The C library can return either a valid object pointer or a null pointer.
        // A subsequent check is made to ensure that the pointer is valid.
        let handle = unsafe { FakeCFeatureLibraryNew() };
        if handle.is_null() {
            return Err(PlatformError::NullHandle);
        };
        Ok(SafeHandle { handle, fake: true })
    }
}

impl Drop for SafeHandle {
    fn drop(&mut self) {
        // SAFETY: The C call frees the memory associated with handle, which is okay since the
        // pointer is dropped immediately afterwards.
        if self.fake {
            unsafe { FakeCFeatureLibraryDelete(self.handle) }
        }
    }
}

/// A fake featured client, used to mock communications to featured via the
/// wrapped fake C library.
///
/// This client never makes actual requests to featured,
/// instead it uses an in-memory store to mock out responses.
pub struct FakePlatformFeatures {
    handle: SafeHandle,
    // Since there are references to these `CString`s stored in an
    // in-memory map managed by the C library, it is important that
    // they are not dropped until the C library is cleaned up and the
    // handle to the library is released, or the feature is specifically
    // cleared. By storing them in a `Vec` in this struct, we guarantee
    // that they always live long enough. By storing those `Vec`s in a
    // `HashMap` under the feature's name as a key, we are able to properly
    // track and remove them when a feature is explicitly cleared.
    c_strings: HashMap<String, Vec<(std::ffi::CString, std::ffi::CString)>>,
}

impl FakePlatformFeatures {
    /// Creates a new client for faking requests to featured.
    ///
    /// # Errors
    ///
    /// If the underlying C calls do not return a proper handle to
    /// the fake featured client, an error string will be returned.
    pub fn new() -> Result<Self, PlatformError> {
        Ok(FakePlatformFeatures {
            handle: SafeHandle::fake()?,
            c_strings: HashMap::default(),
        })
    }

    /// Sets the specified feature's enablement status in the in-memory store
    /// managed by the underlying fake C client.
    ///
    /// The in-memory store uses referential equality to save enablement state,
    /// so it is important to use the same feature for setting and checking the state.
    pub fn set_feature_enabled<'a>(&'a mut self, feature: &'a Feature, is_enabled: bool) {
        // SAFETY: The C call will never invalidate the handle or feature pointers.
        unsafe {
            FakeCFeatureLibrarySetEnabled(
                self.handle.handle,
                feature.c_feature.name,
                is_enabled.into(),
            );
        };
    }

    /// Clears the specified feature's enablement status in the in-memory store
    /// managed by the underlying fake C client.
    pub fn clear_feature_enabled(&mut self, feature: &Feature) {
        // SAFETY: The C call will never invalidate the handle or feature pointers.
        unsafe { FakeCFeatureLibraryClearEnabled(self.handle.handle, feature.c_feature.name) };
    }

    /// Adds or updates value associated with the provided key for the specified feature.
    ///
    /// The in-memory store uses referential equality to save parameter values,
    /// so it is important to use the same feature for setting and checking the state.
    pub fn set_param(&mut self, feature: &Feature, key: &str, value: &str) {
        let c_key = std::ffi::CString::new(key).expect("Unable to convert key");
        let c_value = std::ffi::CString::new(value).expect("Unable to convert value");

        // SAFETY: The C call will never invalidate the handle or feature pointers.
        unsafe {
            FakeCFeatureLibrarySetParam(
                self.handle.handle,
                feature.c_feature.name,
                c_key.as_ptr(),
                c_value.as_ptr(),
            );
        };

        // The key/value `CString`s must be stored to prevent them from being dropped at
        // the end of this function.
        self.c_strings
            .entry(feature.name().to_string())
            .or_default()
            .push((c_key, c_value));
    }
}

impl CheckFeature for FakePlatformFeatures {
    fn is_feature_enabled_blocking(&self, feature: &Feature) -> bool {
        self.handle.is_feature_enabled_blocking(feature)
    }

    fn get_params_and_enabled(
        &self,
        features: &[&Feature],
    ) -> Result<GetParamsAndEnabledResponse, PlatformError> {
        self.handle.get_params_and_enabled_blocking(features)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_properly_fakes_the_feature_library_for_is_enabled() {
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

    #[test]
    fn it_properly_fakes_the_feature_library_for_parameters() {
        let mut subject = FakePlatformFeatures::new().unwrap();

        let feature_one = Feature::new("some-valid-feature", false).unwrap();
        let feature_two = Feature::new("other-valid-feature", false).unwrap();
        let feature_three = Feature::new("another-valid-feature", false).unwrap();

        let param_one_key = "some-param".to_string();
        let param_one_value = "some-value".to_string();
        let param_two_key = "other-param".to_string();
        let param_two_value = "other-value".to_string();

        subject.set_param(&feature_one, &param_one_key, &param_one_value);
        subject.set_param(&feature_one, &param_two_key, &param_two_value);
        subject.set_feature_enabled(&feature_one, true);
        subject.set_feature_enabled(&feature_two, true);

        let actual = subject
            .get_params_and_enabled(&[&feature_one, &feature_two])
            .unwrap();

        assert!(actual.is_enabled(&feature_one));
        assert!(actual.is_enabled(&feature_two));
        assert!(!actual.is_enabled(&feature_three));
        assert!(actual.get_params(&feature_one).is_some());
        assert!(actual.get_params(&feature_two).is_some());
        assert!(actual.get_params(&feature_three).is_none());

        let actual_params = actual.get_params(&feature_one).unwrap();
        assert_eq!(actual_params.len(), 2);
        assert_eq!(actual_params.get(&param_one_key), Some(&param_one_value));
        assert_eq!(actual_params.get(&param_two_key), Some(&param_two_value));

        assert_eq!(
            actual.get_param(&feature_one, &param_one_key),
            Some(&param_one_value)
        );
        assert_eq!(
            actual.get_param(&feature_one, &param_two_key),
            Some(&param_two_value)
        );

        let actual_params = actual.get_params(&feature_two).unwrap();
        assert_eq!(actual_params.len(), 0);
        assert_eq!(actual.get_param(&feature_two, &param_one_key), None);
        assert_eq!(actual.get_param(&feature_two, &param_two_key), None);
    }

}
