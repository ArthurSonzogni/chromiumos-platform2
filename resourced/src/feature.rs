// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;
use std::sync::Arc;
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

type FeatureChangeCallback = Box<dyn Fn(bool) + Send + Sync + 'static>;
type FeatureRegisterInfo = (&'static str, bool, Option<FeatureChangeCallback>);

struct Feature {
    // The cached results of feature query.
    enabled: AtomicBool,

    // Callback invoked when the feature enable state changes.
    #[cfg(feature = "chromeos")]
    cb: Option<FeatureChangeCallback>,

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
    features: Arc<HashMap<String, Feature>>,
}

impl FeatureManager {
    #[cfg_attr(not(feature = "chromeos"), allow(unused_variables))]
    fn new(features: Vec<FeatureRegisterInfo>) -> Result<FeatureManager> {
        let features = features
            .into_iter()
            .map(|(name, default_enabled, cb)| -> Result<(String, Feature)> {
                cfg_if::cfg_if! {
                    if #[cfg(feature = "chromeos")] {
                        let raw = featured::Feature::new(name, default_enabled)?;
                        let enabled =
                            featured::PlatformFeatures::get()?.is_feature_enabled_blocking(&raw);
                        if let Some(cb) = cb.as_ref() {
                            if enabled != default_enabled {
                                cb(enabled);
                            }
                        }

                        let feature = Feature { enabled: AtomicBool::new(enabled), cb, raw };
                        Ok((name.to_owned(), feature))
                    } else {
                        Ok((name.to_owned(), Feature { enabled: AtomicBool::new(default_enabled) }))
                    }
                }
            })
            .collect::<Result<Vec<(String, Feature)>>>()
            .context("failed to initialize features")?;

        Ok(FeatureManager {
            features: Arc::new(HashMap::from_iter(features)),
        })
    }

    // Returns the cached feature query result.
    fn is_feature_enabled(&self, feature_name: &str) -> bool {
        match self.features.get(feature_name) {
            Some(feature) => feature.enabled.load(Ordering::SeqCst),
            None => false,
        }
    }

    #[cfg(feature = "chromeos")]
    fn reload_cache(&self) -> Result<()> {
        let features: Vec<&featured::Feature> = self.features.values().map(|f| &f.raw).collect();
        let resp = featured::PlatformFeatures::get()?
            .get_params_and_enabled(&features)
            .context("failed to query features")?;
        for feature in self.features.values() {
            let new_value = resp.is_enabled(&feature.raw);
            let old_value = feature.enabled.swap(new_value, Ordering::SeqCst);
            if new_value != old_value {
                if let Some(cb) = feature.cb.as_ref() {
                    cb(new_value);
                }
            }
        }
        Ok(())
    }
}

// Singleton pattern.
static FEATURE_MANAGER: OnceCell<FeatureManager> = OnceCell::new();

static PENDING_FEATURES: OnceCell<Mutex<Vec<FeatureRegisterInfo>>> = OnceCell::new();

/// Register a feature flag to be initialized and monitored by [`init`].
///
/// # Arguments
/// * `feature_name` - The name of the feature flag.
/// * `enabled_by_default` - The default value of the flag.
/// * `cb` - An optional callback to be invoked when the feature flag changes. Note
///          that if the initial state of the flag matches `enabled_by_default`, this
///          callback will not be invoked.
pub fn register_feature(
    feature_name: &'static str,
    enabled_by_default: bool,
    cb: Option<FeatureChangeCallback>,
) {
    let pending = PENDING_FEATURES.get_or_init(|| Mutex::new(Vec::new()));
    pending
        .lock()
        .expect("lock failed")
        .push((feature_name, enabled_by_default, cb))
}

#[cfg_attr(not(feature = "chromeos"), allow(unused_variables))]
pub async fn init(conn: &SyncConnection) -> Result<()> {
    let Some(pending) = PENDING_FEATURES.get() else {
        return Ok(());
    };
    let pending = std::mem::take(&mut *pending.lock().expect("lock failed"));
    let feature_manager = FeatureManager::new(pending)?;

    if FEATURE_MANAGER.set(feature_manager).is_err() {
        bail!("Double initialization of FEATURE_MANAGER");
    }

    #[cfg(feature = "chromeos")]
    featured::listen_for_refetch_needed(conn, || {
        let feature_manager = FEATURE_MANAGER
            .get()
            .expect("FEATURE_MANAGER singleton disappeared");
        if let Err(e) = feature_manager.reload_cache() {
            error!("Error reloading feature cache: {:?}", e);
        }
    })
    .await
    .context("failed to start feature monitoring")?;

    Ok(())
}

#[cfg(not(test))]
pub fn is_feature_enabled(feature_name: &str) -> Result<bool> {
    Ok(FEATURE_MANAGER
        .get()
        .context("FEATURE_MANAGER is not initialized")?
        .is_feature_enabled(feature_name))
}

#[cfg(test)]
pub fn is_feature_enabled(_feature_name: &str) -> Result<bool> {
    Ok(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initialize_feature_in_default_state() {
        let feature_manager = FeatureManager::new(vec![
            ("FakeFeatureDisabled", false, None),
            ("FakeFeatureEnabled", true, None),
        ])
        .unwrap();

        assert!(!feature_manager.is_feature_enabled("FakeFeatureDisabled"));
        assert!(feature_manager.is_feature_enabled("FakeFeatureEnabled"));
    }
}
