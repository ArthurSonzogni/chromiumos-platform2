// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libsegmentation/feature_management_fake.h"
#include "libsegmentation/feature_management_interface.h"

namespace segmentation {

namespace fake {

bool FeatureManagementFake::IsFeatureEnabled(const std::string& name) const {
  return system_features_properties_.count(name) > 0;
}

FeatureManagementInterface::FeatureLevel
FeatureManagementFake::GetFeatureLevel() {
  return system_features_level_;
}

FeatureManagementInterface::ScopeLevel FeatureManagementFake::GetScopeLevel() {
  return system_scope_level_;
}

void FeatureManagementFake::SetFeatureLevel(FeatureLevel level) {
  system_features_level_ = level;
}

void FeatureManagementFake::SetScopeLevel(ScopeLevel level) {
  system_scope_level_ = level;
}

void FeatureManagementFake::SetFeature(const std::string& name) {
  system_features_properties_.insert(name);
}

void FeatureManagementFake::UnsetFeature(const std::string& name) {
  system_features_properties_.erase(name);
}

}  // namespace fake

}  // namespace segmentation
