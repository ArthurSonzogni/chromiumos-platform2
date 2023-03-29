// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libsegmentation/feature_management.h>

namespace segmentation {

FeatureManagement::FeatureManagement()
    : FeatureManagement(std::make_unique<FeatureManagementImpl>()) {}

FeatureManagement::FeatureManagement(
    std::unique_ptr<FeatureManagementInterface> impl)
    : impl_(std::move(impl)) {}

bool FeatureManagement::IsFeatureEnabled(const std::string& name) const {
  return impl_->IsFeatureEnabled(name);
}

int FeatureManagement::GetFeatureLevel() const {
  return impl_->GetFeatureLevel();
}

}  // namespace segmentation
