// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_delegate.h"

#include <utility>
#include <vector>

#include <base/optional.h>
#include <gtest/gtest.h>

namespace power_manager {
namespace system {

class AmbientLightSensorDelegateTest
    : public testing::TestWithParam<
          std::pair<std::vector<base::Optional<int>>, base::Optional<int>>> {};

TEST_P(AmbientLightSensorDelegateTest, CheckCalculateColorTemperature) {
  ASSERT_EQ(GetParam().first.size(), 3);
  std::map<ChannelType, int> readings;
  for (size_t i = 0; i < 3; ++i) {
    if (!GetParam().first[i].has_value())
      continue;

    readings[static_cast<ChannelType>(i)] = GetParam().first[i].value();
  }

  EXPECT_EQ(AmbientLightSensorDelegate::CalculateColorTemperature(readings),
            GetParam().second);
}

INSTANTIATE_TEST_SUITE_P(
    AmbientLightSensorDelegateTestRun,
    AmbientLightSensorDelegateTest,
    ::testing::Values(
        std::make_pair(std::vector<base::Optional<int>>{base::nullopt, 1, 1},
                       base::nullopt),
        std::make_pair(std::vector<base::Optional<int>>{1, base::nullopt, 1},
                       base::nullopt),
        std::make_pair(std::vector<base::Optional<int>>{1, 1, base::nullopt},
                       base::nullopt),
        std::make_pair(std::vector<base::Optional<int>>{100, 10, 100},
                       base::nullopt),
        std::make_pair(std::vector<base::Optional<int>>{50, 50, 50}, 5458),
        std::make_pair(std::vector<base::Optional<int>>{100, 100, 100}, 5458),
        std::make_pair(std::vector<base::Optional<int>>{50, 50, 100}, 20921),
        std::make_pair(std::vector<base::Optional<int>>{50, 60, 60}, 7253)));

}  // namespace system
}  // namespace power_manager
