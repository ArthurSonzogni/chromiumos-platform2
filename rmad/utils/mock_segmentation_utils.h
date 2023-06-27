// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_SEGMENTATION_UTILS_H_
#define RMAD_UTILS_MOCK_SEGMENTATION_UTILS_H_

#include "rmad/utils/segmentation_utils.h"

#include "gmock/gmock.h"

namespace rmad {

class MockSegmentationUtils : public SegmentationUtils {
 public:
  MockSegmentationUtils() = default;
  ~MockSegmentationUtils() override = default;

  MOCK_METHOD(bool, IsFeatureEnabled, (), (const, override));
  MOCK_METHOD(bool, IsFeatureProvisioned, (), (const, override));
  MOCK_METHOD(int, GetFeatureLevel, (), (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_SEGMENTATION_UTILS_H_
