// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::str::FromStr;
use std::sync::Arc;
use std::sync::Mutex;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use dbus::nonblock::SyncConnection;
#[cfg(feature = "chromeos")]
use featured::CheckFeature; // Trait CheckFeature is for get_params_and_enabled
use once_cell::sync::OnceCell;

use crate::sync::NoPoison;

type FeatureChangeCallback = Box<dyn Fn(bool) + Send + Sync + 'static>;
type FeatureRegisterInfo = (&'static str, bool, Option<FeatureChangeCallback>);

// The state of a feature that can be changed.
struct FeatureState {
    // Whether the feature is enabled.
    enabled: bool,

    // The params of the feature. If the feature is disabled or has no params
    // this map will be empty.
    params: HashMap<String, String>,
}

struct Feature {
    // The state of the feature.
    state: Arc<Mutex<FeatureState>>,

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

                        // Initialize to default values for now.
                        // init() below immediately reloads the cache after creating
                        // the FeatureManager instance which will set the correct actual values.
                        let feature_state = FeatureState {
                            enabled: default_enabled,
                            params: HashMap::new(),
                        };

                        let feature = Feature {
                            state: Arc::new(Mutex::new(feature_state)),
                            cb,
                            raw
                        };
                        Ok((name.to_owned(), feature))
                    } else {
                        Ok((name.to_owned(), Feature {
                            state: Arc::new(Mutex::new(FeatureState {
                                enabled: default_enabled,
                                params: HashMap::new(),
                            })),
                        }))
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
            Some(feature) => {
                let state = feature.state.do_lock();
                state.enabled
            }
            None => false,
        }
    }

    fn get_feature_param_as<T: FromStr>(
        &self,
        feature_name: &str,
        param_name: &str,
    ) -> Result<Option<T>>
    where
        <T as FromStr>::Err: std::error::Error + Send + Sync + 'static,
    {
        if let Some(feature) = self.features.get(feature_name) {
            let state = feature.state.do_lock();
            if let Some(value) = state.params.get(param_name) {
                return Ok(Some(value.parse::<T>().with_context(|| {
                    format!("Failed to parse param {}={}", param_name, value)
                })?));
            }
        }
        Ok(None)
    }

    fn reload_cache(&self) -> Result<()> {
        cfg_if::cfg_if! {
            if #[cfg(feature = "chromeos")] {
                let features: Vec<&featured::Feature> =
                    self.features.values().map(|f| &f.raw).collect();
                let resp = featured::PlatformFeatures::get()?
                    .get_params_and_enabled(&features)
                    .context("failed to query features")?;
                for feature in self.features.values() {
                    let (run_callback, new_enabled) = {
                        let mut state = feature.state.do_lock();

                        let old_enabled = state.enabled;

                        let new_params = match resp.get_params(&feature.raw) {
                            Some(params) => params.clone(),
                            None => HashMap::new(),
                        };

                        let old_params = std::mem::replace(&mut state.params, new_params);

                        state.enabled = resp.is_enabled(&feature.raw);

                        (
                            old_enabled != state.enabled || old_params != state.params,
                            state.enabled
                        )
                    };

                    if run_callback {
                        if let Some(cb) = feature.cb.as_ref() {
                            cb(new_enabled);
                        }
                    }
                }
                Ok(())
            } else {
                Ok(())
            }
        }
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
    assert!(
        FEATURE_MANAGER.get().is_none(),
        "Features cannot be resgistered after FeatureManager initialization"
    );

    let pending = PENDING_FEATURES.get_or_init(|| Mutex::new(Vec::new()));
    pending
        .do_lock()
        .push((feature_name, enabled_by_default, cb))
}

#[cfg_attr(not(feature = "chromeos"), allow(unused_variables))]
pub async fn init(conn: &SyncConnection) -> Result<()> {
    let Some(pending) = PENDING_FEATURES.get() else {
        return Ok(());
    };
    let pending = std::mem::take(&mut *pending.do_lock());
    let feature_manager = FeatureManager::new(pending)?;

    if FEATURE_MANAGER.set(feature_manager).is_err() {
        bail!("Double initialization of FEATURE_MANAGER");
    }

    FEATURE_MANAGER
        .get()
        .expect("FEATURE_MANAGER singleton disappeared")
        .reload_cache()
        .context("failed to load initial feature state")?;

    #[cfg(feature = "chromeos")]
    featured::listen_for_refetch_needed(conn, || {
        let feature_manager = FEATURE_MANAGER
            .get()
            .expect("FEATURE_MANAGER singleton disappeared");
        if let Err(e) = feature_manager.reload_cache() {
            log::error!("Error reloading feature cache: {:?}", e);
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

pub fn get_feature_param_as<T: FromStr>(feature_name: &str, param_name: &str) -> Result<Option<T>>
where
    <T as FromStr>::Err: std::error::Error + Send + Sync + 'static,
{
    FEATURE_MANAGER
        .get()
        .context("FEATURE_MANAGER is not initialized")?
        .get_feature_param_as(feature_name, param_name)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set_feature_param(
        feature_manager: &FeatureManager,
        feature_name: &str,
        param_name: String,
        value: String,
    ) {
        let mut state = feature_manager
            .features
            .get(feature_name)
            .unwrap()
            .state
            .lock()
            .unwrap();
        state.params.insert(param_name, value);
    }

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

    #[test]
    fn test_feature_param_parsing() {
        let feature_manager = FeatureManager::new(vec![("FakeFeature", true, None)]).unwrap();
        set_feature_param(
            &feature_manager,
            "FakeFeature",
            "FakeFeatureParamInteger".to_string(),
            "12345".to_string(),
        );
        set_feature_param(
            &feature_manager,
            "FakeFeature",
            "FakeFeatureParamString".to_string(),
            "abc".to_string(),
        );

        assert_eq!(
            feature_manager
                .get_feature_param_as::<u64>("FakeFeature", "FakeFeatureParamInteger")
                .unwrap(),
            Some(12345)
        );
        assert_eq!(
            feature_manager
                .get_feature_param_as::<String>("FakeFeature", "FakeFeatureParamString")
                .unwrap(),
            Some("abc".to_string())
        );
        assert!(feature_manager
            .get_feature_param_as::<u64>("FakeFeature", "FakeFeatureParamString")
            .is_err());
        assert!(feature_manager
            .get_feature_param_as::<u64>("FakeFeature", "NonExistParam")
            .unwrap()
            .is_none());
        assert!(feature_manager
            .get_feature_param_as::<u64>("FakeFeatureNonExist", "FakeFeatureParamInteger")
            .unwrap()
            .is_none());
    }
}
