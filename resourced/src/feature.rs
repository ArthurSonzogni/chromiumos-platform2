// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::Mutex;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use dbus::nonblock::SyncConnection;
#[cfg(feature = "chromeos")]
use featured::CheckFeature;
#[cfg(feature = "chromeos")]
use log::error;
use once_cell::sync::OnceCell; // Trait CheckFeature is for is_feature_enabled_blocking

struct Feature {
    // The cached results of feature query.
    enabled: bool,

    // There must only ever be one struct instance for a given feature name.
    //
    // Reference: https://chromium.googlesource.com/chromiumos/platform2/+/79195b9779a292e50cef56b609ea089bd92f2175/featured/c_feature_library.h#25
    #[cfg(feature = "chromeos")]
    raw: featured::Feature,
}

// Only use featured in ebuild as using featured makes "cargo build" fail.
//
// Reference: https://chromium.googlesource.com/chromiumos/platform2/+/main/featured/README.md
struct FeatureManager {
    features: HashMap<String, Feature>,
}

impl FeatureManager {
    fn new() -> FeatureManager {
        FeatureManager {
            features: HashMap::new(),
        }
    }

    // Returns the cached feature query result.
    fn is_feature_enabled(&self, feature_name: &str) -> Result<bool> {
        match self.features.get(feature_name) {
            Some(feature) => Ok(feature.enabled),
            None => Ok(false),
        }
    }

    // Adds a feature to the hashmap if it's not present and caches the feature query.
    fn initialize_feature(&mut self, feature_name: &str, enabled_by_default: bool) -> Result<()> {
        let Entry::Vacant(vacant_entry) = self.features.entry(feature_name.to_string()) else {
            bail!("Double initialization of {}", feature_name);
        };

        cfg_if::cfg_if! {
            if #[cfg(feature = "chromeos")] {
                let feature = featured::Feature::new(feature_name, enabled_by_default)?;
                let enabled =
                    featured::PlatformFeatures::get()?.is_feature_enabled_blocking(&feature);
                vacant_entry.insert(Feature { enabled, raw: feature });
            } else {
                vacant_entry.insert(Feature { enabled: enabled_by_default });
            }
        }

        Ok(())
    }

    #[cfg(feature = "chromeos")]
    fn reload_cache(&mut self) -> Result<()> {
        let features: Vec<&featured::Feature> = self.features.values().map(|f| &f.raw).collect();
        let resp = featured::PlatformFeatures::get()?
            .get_params_and_enabled(&features)
            .context("failed to query features")?;
        for feature in self.features.values_mut() {
            feature.enabled = resp.is_enabled(&feature.raw);
        }
        Ok(())
    }
}

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

#[cfg_attr(not(feature = "chromeos"), allow(unused_variables))]
pub async fn start_feature_monitoring(conn: &SyncConnection) -> Result<()> {
    if FEATURE_MANAGER.get().is_none() {
        bail!("FEATURE_MANAGER is not initialized");
    }

    #[cfg(feature = "chromeos")]
    featured::listen_for_refetch_needed(conn, || {
        let mut feature_manager = FEATURE_MANAGER
            .get()
            .expect("FEATURE_MANAGER singleton disappeared")
            .lock()
            .expect("Lock failed");
        if let Err(e) = feature_manager.reload_cache() {
            error!("Error reloading feature cache: {:?}", e);
        }
    })
    .await
    .context("failed to start feature monitoring")?;

    Ok(())
}

#[cfg(test)]
pub fn init_for_test() {
    let _ = init();
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

pub fn initialize_feature(feature_name: &str, enabled_by_default: bool) -> Result<()> {
    let feature_manager = FEATURE_MANAGER
        .get()
        .context("FEATURE_MANAGER is not initialized")?;
    if let Ok(mut feature_manager_lock) = feature_manager.lock() {
        feature_manager_lock.initialize_feature(feature_name, enabled_by_default)
    } else {
        bail!("Failed to lock FEATURE_MANAGER");
    }
}
