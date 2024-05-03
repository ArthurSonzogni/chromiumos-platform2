// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/features/regmon_features_impl.h"

#include <base/check.h>
#include <featured/feature_library.h>

namespace regmon::features {

RegmonFeaturesImpl::RegmonFeaturesImpl(
    feature::PlatformFeaturesInterface* features_lib)
    : features_lib_(features_lib) {}

bool RegmonFeaturesImpl::PolicyMonitoringEnabled() {
  CHECK(features_lib_);
  return features_lib_->IsEnabledBlocking(kRegmonPolicyMonitoringEnabled);
}

}  // namespace regmon::features
