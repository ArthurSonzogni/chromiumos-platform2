// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_SEGMENTATION_UTILS_H_
#define RMAD_UTILS_FAKE_SEGMENTATION_UTILS_H_

#include "rmad/utils/segmentation_utils.h"

namespace rmad {

class FakeSegmentationUtils : public SegmentationUtils {
 public:
  explicit FakeSegmentationUtils(bool is_feature_enabled,
                                 bool is_feature_provisioned,
                                 int feature_level)
      : is_feature_enabled_(is_feature_enabled),
        is_feature_provisioned_(is_feature_provisioned),
        feature_level_(feature_level) {}
  ~FakeSegmentationUtils() override = default;

  bool IsFeatureEnabled() const override { return is_feature_enabled_; }
  bool IsFeatureProvisioned() const override { return is_feature_provisioned_; }
  int GetFeatureLevel() const override { return feature_level_; }

 private:
  bool is_feature_enabled_;
  bool is_feature_provisioned_;
  int feature_level_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_SEGMENTATION_UTILS_H_
