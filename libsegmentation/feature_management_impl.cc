// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libsegmentation/feature_management_impl.h>

#include <string>

namespace segmentation {

bool FeatureManagementImpl::IsFeatureEnabled(const std::string& name) const {
  // To be continued.
  return false;
}

int FeatureManagementImpl::GetFeatureLevel() const {
  // To be continued.
  return 0;
}

}  // namespace segmentation
