// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use once_cell::sync::OnceCell;

#[cfg(feature = "chromeos")]
use featured::CheckFeature; // Trait CheckFeature is for is_feature_enabled_blocking

// Only use featured in ebuild as using featured makes "cargo build" fail.
//
// Reference: https://chromium.googlesource.com/chromiumos/platform2/+/main/featured/README.md
struct FeatureManager {
    // Contains the cached results of feature queries.
    feature_caches: HashMap<String, bool>,

    // There must only ever be one struct instance for a given feature name.
    //
    // Reference: https://chromium.googlesource.com/chromiumos/platform2/+/79195b9779a292e50cef56b609ea089bd92f2175/featured/c_feature_library.h#25
    #[cfg(feature = "chromeos")]
    features: HashMap<String, featured::Feature>,
}

impl FeatureManager {
    fn new() -> FeatureManager {
        FeatureManager {
            feature_caches: HashMap::new(),

            #[cfg(feature = "chromeos")]
            features: HashMap::new(),
        }
    }

    // Returns the cached feature query result.
    fn is_feature_enabled(&self, feature_name: &str) -> Result<bool> {
        match self.feature_caches.get(feature_name) {
            Some(value) => Ok(*value),
            None => Ok(false),
        }
    }

    // Adds a feature to the hashmap if it's not present and caches the feature query.
    fn update_feature(&mut self, feature_name: &str) -> Result<()> {
        #[cfg(feature = "chromeos")]
        if !self.features.contains_key(feature_name) {
            let feature = featured::Feature::new(feature_name, false)?;
            self.features.insert(feature_name.to_string(), feature);
        }

        #[cfg(feature = "chromeos")]
        let feature_enabled = featured::PlatformFeatures::get()?.is_feature_enabled_blocking(
            self.features
                .get(feature_name)
                .context("No feature entry in HashMap")?,
        );

        #[cfg(not(feature = "chromeos"))]
        let feature_enabled = false;

        self.feature_caches
            .insert(feature_name.to_string(), feature_enabled);
        Ok(())
    }
}

// FeatureManager is thread safe because its content is protected by a Mutex.
unsafe impl Send for FeatureManager {}

// Singleton pattern.
static FEATURE_MANAGER: OnceCell<Mutex<FeatureManager>> = OnceCell::new();

pub fn init() -> Result<()> {
    if FEATURE_MANAGER
        .set(Mutex::new(FeatureManager::new()))
        .is_err()
    {
        bail!("Failed to set FEATURE_MANAGER");
    }
    Ok(())
}

pub fn is_feature_enabled(feature_name: &str) -> Result<bool> {
    let feature_manager = FEATURE_MANAGER
        .get()
        .context("FEATURE_MANAGER is not initialized")?;
    if let Ok(feature_manager_lock) = feature_manager.lock() {
        feature_manager_lock.is_feature_enabled(feature_name)
    } else {
        bail!("Failed to lock FEATURE_MANAGER");
    }
}

pub fn update_feature(feature_name: &str) -> Result<()> {
    let feature_manager = FEATURE_MANAGER
        .get()
        .context("FEATURE_MANAGER is not initialized")?;
    if let Ok(mut feature_manager_lock) = feature_manager.lock() {
        feature_manager_lock.update_feature(feature_name)
    } else {
        bail!("Failed to lock FEATURE_MANAGER");
    }
}
