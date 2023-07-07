// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_
#define RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_

#include "rmad/segmentation/segmentation_utils.h"

namespace rmad {

class FakeSegmentationUtils : public SegmentationUtils {
 public:
  explicit FakeSegmentationUtils(bool is_feature_enabled,
                                 bool is_feature_mutable,
                                 int feature_level)
      : is_feature_enabled_(is_feature_enabled),
        is_feature_mutable_(is_feature_mutable),
        feature_level_(feature_level) {}
  ~FakeSegmentationUtils() override = default;

  bool IsFeatureEnabled() const override { return is_feature_enabled_; }
  bool IsFeatureMutable() const override { return is_feature_mutable_; }
  int GetFeatureLevel() const override { return feature_level_; }
  bool GetFeatureFlags(bool* is_chassis_branded,
                       int* hw_compliance_version) const override {
    *is_chassis_branded = is_chassis_branded_;
    *hw_compliance_version = hw_compliance_version_;
    return true;
  }
  bool SetFeatureFlags(bool is_chassis_branded,
                       int hw_compliance_version) override {
    is_chassis_branded_ = is_chassis_branded;
    hw_compliance_version_ = hw_compliance_version;
    return true;
  }

 private:
  bool is_feature_enabled_;
  bool is_feature_mutable_;
  int feature_level_;
  // The two values actually affect |feature_level_|, and the logic is
  // implemented in libsegmentation. However, the values are decoupled in this
  // fake class.
  bool is_chassis_branded_;
  int hw_compliance_version_;
};

}  // namespace rmad

#endif  // RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_
