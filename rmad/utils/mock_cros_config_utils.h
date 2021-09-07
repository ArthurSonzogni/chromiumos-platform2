// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_
#define RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_

#include "rmad/utils/cros_config_utils.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>

namespace rmad {

class MockCrosConfigUtils : public CrosConfigUtils {
 public:
  MockCrosConfigUtils() = default;
  ~MockCrosConfigUtils() override = default;

  MOCK_METHOD(bool, GetModelName, (std::string*), (const, override));
  MOCK_METHOD(bool, GetCurrentSkuId, (int*), (const, override));
  MOCK_METHOD(bool, GetCurrentWhitelabelTag, (std::string*), (const, override));
  MOCK_METHOD(bool, GetSkuIdList, (std::vector<int>*), (const override));
  MOCK_METHOD(bool,
              GetWhitelabelTagList,
              (std::vector<std::string>*),
              (const override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_
