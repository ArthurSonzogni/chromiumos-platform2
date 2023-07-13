// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
#define RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_

#include "rmad/segmentation/segmentation_utils.h"

#include <memory>

#include <libsegmentation/feature_management.h>

namespace rmad {

class SegmentationUtilsImpl : public SegmentationUtils {
 public:
  SegmentationUtilsImpl() = default;
  // Used to inject FeatureManagementInterface for testing.
  explicit SegmentationUtilsImpl(
      std::unique_ptr<segmentation::FeatureManagementInterface>
          feature_management_interface);
  ~SegmentationUtilsImpl() override = default;

  bool IsFeatureEnabled() const override;
  bool IsFeatureProvisioned() const override;
  int GetFeatureLevel() const override;

 private:
  segmentation::FeatureManagement feature_management_;
};

}  // namespace rmad

#endif  // RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
