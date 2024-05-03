// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_FEATURES_REGMON_FEATURES_IMPL_H_
#define REGMON_FEATURES_REGMON_FEATURES_IMPL_H_

#include <featured/feature_library.h>

#include "regmon/features/regmon_features.h"

namespace regmon::features {

class RegmonFeaturesImpl : public RegmonFeatures {
 public:
  static constexpr VariationsFeature kRegmonPolicyMonitoringEnabled = {
      .name = "CrOSLateBootRegmonPolicyMonitoringEnabled",
      .default_state = FEATURE_DISABLED_BY_DEFAULT,
  };

  explicit RegmonFeaturesImpl(feature::PlatformFeaturesInterface* features_lib);
  RegmonFeaturesImpl(const RegmonFeaturesImpl&) = delete;
  RegmonFeaturesImpl& operator=(const RegmonFeaturesImpl&) = delete;

  bool PolicyMonitoringEnabled();

 private:
  feature::PlatformFeaturesInterface* features_lib_;
};

}  // namespace regmon::features

#endif  // REGMON_FEATURES_REGMON_FEATURES_IMPL_H_
