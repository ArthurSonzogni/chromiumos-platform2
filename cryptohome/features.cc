// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/features.h"

#include <memory>
#include <utility>
#include "base/functional/bind.h"

#include <base/check.h>
#include <base/no_destructor.h>
#include <featured/fake_platform_features.h>
#include <featured/feature_library.h>

namespace cryptohome {
namespace {

const VariationsFeature& GetVariationFeatureFor(
    Features::ActiveFeature active_feature) {
  switch (active_feature) {
    case Features::kUSSMigration:
      return kCrOSLateBootMigrateToUserSecretStash;
    case Features::kModernPin:
      return kCrOSLateBootEnableModernPin;
    case Features::KMigratePin:
      return kCrOSLateBootMigrateToModernPin;
  }
}

}  // namespace

Features::Features(scoped_refptr<dbus::Bus> bus, bool test_instance)
    : test_instance_(test_instance) {
  if (test_instance) {
    auto fake_feature_lib =
        std::make_unique<feature::FakePlatformFeatures>(bus);
    fake_platform_features_ptr_ = fake_feature_lib.get();
    feature_lib_ = std::move(fake_feature_lib);
  } else {
    fake_platform_features_ptr_ = nullptr;
    feature_lib_ = feature::PlatformFeatures::New(bus);
  }
}

bool Features::IsFeatureEnabled(ActiveFeature active_feature) {
  return feature_lib_->IsEnabledBlocking(
      GetVariationFeatureFor(active_feature));
}

void Features::SetDefaultForFeature(ActiveFeature active_feature,
                                    bool enabled) {
  CHECK(test_instance_);
  fake_platform_features_ptr_->SetEnabled(
      GetVariationFeatureFor(active_feature).name, enabled);
}

AsyncInitFeatures::AsyncInitFeatures(
    base::RepeatingCallback<Features*()> getter)
    : getter_(std::move(getter)) {}

AsyncInitFeatures::AsyncInitFeatures(Features& features)
    : AsyncInitFeatures(base::BindRepeating(
          [](Features* features) { return features; }, &features)) {}

bool AsyncInitFeatures::IsFeatureEnabled(
    Features::ActiveFeature active_feature) {
  if (Features* features = getter_.Run()) {
    return features->IsFeatureEnabled(active_feature);
  }
  const auto& variations_feature = GetVariationFeatureFor(active_feature);
  return variations_feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
}

}  // namespace cryptohome
