// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/segmentation_utils_impl.h"

#include <memory>
#include <utility>

#include <libsegmentation/feature_management.h>

namespace rmad {

SegmentationUtilsImpl::SegmentationUtilsImpl(
    std::unique_ptr<segmentation::FeatureManagementInterface>
        feature_management_interface)
    : feature_management_(std::move(feature_management_interface)) {}

bool SegmentationUtilsImpl::IsFeatureEnabled() const {
  // TODO(chenghan): Get the allowlist from DLM payload.
  return false;
}

bool SegmentationUtilsImpl::IsFeatureProvisioned() const {
  // TODO(chenghan): Check GSC values.
  return true;
}

int SegmentationUtilsImpl::GetFeatureLevel() const {
  return feature_management_.GetFeatureLevel();
}

}  // namespace rmad
