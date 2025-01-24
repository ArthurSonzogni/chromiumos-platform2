// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FEATURES_H_
#define CRYPTOHOME_FEATURES_H_

#include <cstddef>
#include <memory>

#include <base/functional/callback.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

namespace cryptohome {

// Control switch value for generating recoverable key stores.
inline constexpr struct VariationsFeature
    kCrOSLateBootGenerateRecoverableKeyStore = {
        .name = "CrOSLateBootGenerateRecoverableKeyStore",
        .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

// Control switch value for legacy fingerprint migration.
inline constexpr struct VariationsFeature kCrOSMigrateLegacyFingerprint = {
    .name = "CrOSLateBootMigrateLegacyFingerprint",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

// Control switch value using pinweaver to back password credentials.
inline constexpr struct VariationsFeature kCrOSPinweaverForPassword = {
    .name = "CrOSLateBootPinweaverForPassword",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

// Features is a class which is a wrapper around PlatformFeatureInterface, which
// can also be configured for testing.
class Features {
 public:
  // ActiveFeature is an enum that defines active features that are being rolled
  // out via finch in cryptohome.
  enum ActiveFeature {
    kGenerateRecoverableKeyStore,
    kMigrateLegacyFingerprint,
    kPinweaverForPassword,
  };

  // Platform feature library can only initialized with a bus instance.
  explicit Features(scoped_refptr<dbus::Bus> bus,
                    feature::PlatformFeaturesInterface*);

  Features(const Features&) = delete;
  Features& operator=(const Features&) = delete;
  ~Features() = default;

  // Fetches the value from the finch server using the feature library.
  bool IsFeatureEnabled(ActiveFeature active_feature) const;

 private:
  feature::PlatformFeaturesInterface* feature_lib_;
};

// Thin wrapper around a Features object that is asynchronously initialized.
// Because the standard Features object depends on D-Bus, it can't generally be
// initialized at program startup. This makes it difficult to use in other
// objects constructed at start up time. The wrapper simplifies this by
// providing an object that checks if the wrapped instance is available yet, and
// falls back to the default value if it is not.
class AsyncInitFeatures {
 public:
  // Construct a wrapper around a callback that will return null until the
  // Features object is available.
  explicit AsyncInitFeatures(base::RepeatingCallback<Features*()> getter);

  // Construct a wrapper around a pre-existing features object that always
  // exists. This seems redundant (why wrap the object at all?) but is helpful
  // when testing uses that normally need to be wrapped but don't need to be
  // wrapped in test.
  explicit AsyncInitFeatures(Features& features);

  AsyncInitFeatures(const AsyncInitFeatures&) = delete;
  AsyncInitFeatures& operator=(const AsyncInitFeatures&) = delete;

  // Provides the same value as Features::IsFeatureEnabled if it is available,
  // otherwise provides the default value for the feature.
  bool IsFeatureEnabled(Features::ActiveFeature active_feature) const;

 private:
  base::RepeatingCallback<Features*()> getter_;
};

// Helper function used by production and fake code.
const VariationsFeature& GetVariationFeatureFor(
    Features::ActiveFeature active_feature);

}  // namespace cryptohome

#endif  // CRYPTOHOME_FEATURES_H_
