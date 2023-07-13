// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_
#define RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_

#include <base/files/file_path.h>

#include "rmad/segmentation/segmentation_utils.h"

namespace rmad {

class FakeSegmentationUtils : public SegmentationUtils {
 public:
  explicit FakeSegmentationUtils(const base::FilePath& working_dir_path_);
  ~FakeSegmentationUtils() override = default;

  bool IsFeatureEnabled() const override { return is_feature_enabled_; }
  bool IsFeatureMutable() const override { return is_feature_mutable_; }
  int GetFeatureLevel() const override { return feature_level_; }
  bool GetFeatureFlags(bool* is_chassis_branded,
                       int* hw_compliance_version) const override;
  bool SetFeatureFlags(bool is_chassis_branded,
                       int hw_compliance_version) override;

 private:
  base::FilePath working_dir_path_;
  bool is_feature_enabled_;
  bool is_feature_mutable_;
  int feature_level_;
};

}  // namespace rmad

#endif  // RMAD_SEGMENTATION_FAKE_SEGMENTATION_UTILS_H_
