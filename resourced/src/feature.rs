// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use featured::{CheckFeature, Feature, PlatformFeatures};
use std::sync::atomic::{AtomicBool, Ordering};

pub const CROS_FEATURE_MEDIA_DYNAMIC_CGROUP: &str = "CrOSLateBootMediaDynamicCgroup";

// CROS_FEATURES can keep track of CrOS features resourced would be interested in.
const CROS_FEATURES: [(&str, bool); 1] = [
    // New feature can be added below with formant of (feature_name, enabled_by_default)
    (CROS_FEATURE_MEDIA_DYNAMIC_CGROUP, false),
];

pub struct CrOSFeature<'a> {
    // Feature name.
    pub name: &'a str,
    // Feature enablement status by default.
    pub enabled_by_default: bool,
    // If Feature creation fails, feature will have None.
    pub feature: Option<Feature>,
}

impl CrOSFeature<'_> {
    // Return feature enablement status.
    // If Feature creation fails, ERR will be returned.
    pub fn feature_enabled(&self, platform_features: &PlatformFeatures) -> Result<bool> {
        match &self.feature {
            Some(feature) => Ok(platform_features.is_feature_enabled_blocking(feature)),
            None => bail!(
                "Failed to create Feature for name{}, enabled_by_default{}",
                &self.name,
                &self.enabled_by_default
            ),
        }
    }
}

// We need this to ensure that only one valid CrOSFeatures exists.
static CROS_FEATURE_EXISTS: AtomicBool = AtomicBool::new(false);

pub struct CrOSFeatureProvider<'a> {
    feature_list: Vec<CrOSFeature<'a>>,
    platform_features: PlatformFeatures,
}

impl CrOSFeatureProvider<'_> {
    pub fn new() -> Result<Self> {
        // Check this is the first time that CrOSFeatures is being created.
        match CROS_FEATURE_EXISTS.compare_exchange(
            false,
            true,
            Ordering::Relaxed,
            Ordering::Relaxed,
        ) {
            // Populate feature_list if this is the very first instance.
            Ok(_) => {
                let mut feature_list = Vec::new();

                // Loop through the CROS_FEATURES list
                for feature in CROS_FEATURES.iter() {
                    // Create PlatformFeature and add to PlatformFeatureList.
                    // If Feature::new fails, cros_feature will be None.
                    let cros_feature = Feature::new(feature.0, feature.1).ok();

                    feature_list.push(CrOSFeature {
                        name: feature.0,
                        enabled_by_default: feature.1,
                        feature: cros_feature,
                    })
                }

                Ok(CrOSFeatureProvider {
                    feature_list,
                    platform_features: PlatformFeatures::new()?,
                })
            }
            // Create empty feature_list if this is not the first time.
            // This is to ensure that feature_list is being created only once.
            Err(_) => bail!("Failed to create CrOSFeatureProvider since it already exitis!"),
        }
    }
}

impl Drop for CrOSFeatureProvider<'_> {
    fn drop(&mut self) {
        CROS_FEATURE_EXISTS.store(false, Ordering::Relaxed)
    }
}

pub trait FeatureProvider {
    fn feature_enabled(&self, feature_name: &str) -> Result<bool>;
}

impl FeatureProvider for CrOSFeatureProvider<'_> {
    fn feature_enabled(&self, name: &str) -> Result<bool> {
        match self
            .feature_list
            .iter()
            .find(|&feature| feature.name == name)
        {
            Some(feature_item) => feature_item.feature_enabled(&self.platform_features),
            None => bail!("Not able to find Feature! {}", &name),
        }
    }
}
