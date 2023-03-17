// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FEATURES_H_
#define CRYPTOHOME_FEATURES_H_

#include <cstddef>
#include <memory>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>
#include <featured/fake_platform_features.h>

namespace cryptohome {

// Control switch value for migrating existing vaultkeyset users to AuthFactor
// and USS.
inline constexpr struct VariationsFeature
    kCrOSLateBootMigrateToUserSecretStash = {
        .name = "CrOSLateBootMigrateToUserSecretStash",
        .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

// Features is a class which is a wrapper around PlatformFeatureInterface, which
// can also be configured for testing.
class Features {
 public:
  // ActiveFeature is an enum that defines active features that are being rolled
  // out via finch in cryptohome.
  enum ActiveFeature {
    // Feature to enable migration of existing VaultKeysets to AUthFactor and
    // USS.
    kUSSMigration
  };

  // Platform feature library can only initialized with a bus instance.
  explicit Features(scoped_refptr<dbus::Bus> bus, bool test_instance = false);
  Features(const Features&) = delete;
  Features& operator=(const Features&) = delete;
  Features() = delete;
  ~Features() = default;

  // Fetches the value from the finch server using the feature library.
  bool IsFeatureEnabled(ActiveFeature active_feature);

  // SetDefaultForFeature will set the default value for test for the feature.
  // This will be passed down to the feature library which inturn returns the
  // defaults.
  void SetDefaultForFeature(ActiveFeature active_feature, bool enabled);

 protected:
  // Get the variation feature that is used to fetch the feature value
  // from the finch server.
  const VariationsFeature& GetVariationFeatureFor(ActiveFeature active_feature);
  // fake_platform_features_ptr_ is used for testing instances. The pointer is
  // owned by this class.
  feature::FakePlatformFeatures* fake_platform_features_ptr_ = nullptr;
  // Default feature library used to fetch features values from finch.
  std::unique_ptr<feature::PlatformFeaturesInterface> feature_lib_;
  bool test_instance_ = false;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FEATURES_H_
