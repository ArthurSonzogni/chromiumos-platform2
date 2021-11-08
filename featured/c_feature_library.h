// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FEATURED_C_FEATURE_LIBRARY_H_
#define FEATURED_C_FEATURE_LIBRARY_H_

#include "featured/feature_export.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Specifies whether a given feature is enabled or disabled by default.
// NOTE: The actual runtime state may be different, due to a field trial or a
// command line switch.
enum FEATURE_EXPORT FeatureState {
  FEATURE_DISABLED_BY_DEFAULT,
  FEATURE_ENABLED_BY_DEFAULT,
};

// The Feature struct is used to define the default state for a feature. See
// comment below for more details. There must only ever be one struct instance
// for a given feature name - generally defined as a constant global variable or
// file static. It should never be used as a constexpr as it breaks
// pointer-based identity lookup.
struct FEATURE_EXPORT Feature {
  // The name of the feature. This should be unique to each feature and is used
  // for enabling/disabling features via command line flags and experiments.
  // It is strongly recommended to use CamelCase style for feature names, e.g.
  // "MyGreatFeature".
  // In almost all cases, your feature name should start with "CrOSLateBoot",
  // otherwise the lookup will fail.
  const char* const name;

  // The default state (i.e. enabled or disabled) for this feature.
  // NOTE: The actual runtime state may be different, due to a field trial or a
  // command line switch.
  const enum FeatureState default_state;
};

typedef struct CFeatureLibraryOpaque* CFeatureLibrary;

// C wrapper for PlatformFeatures::New()
CFeatureLibrary FEATURE_EXPORT CFeatureLibraryNew();

// C wrapper for PlatformFeatures::~PlatformFeatures()
void FEATURE_EXPORT CFeatureLibraryDelete(CFeatureLibrary handle);

// C wrapper for PlatformFeatures::IsEnabled is NOT defined, since different
// language thread runtimes will likely be incompatible with C++'s
// SequencedTaskRunner.

// C wrapper for PlatformFeatures::IsEnabledBlocking
int FEATURE_EXPORT CFeatureLibraryIsEnabledBlocking(
    CFeatureLibrary handle, const struct Feature* const feature);

#if defined(__cplusplus)
}  // extern "C"
#endif
#endif  // FEATURED_C_FEATURE_LIBRARY_H_
