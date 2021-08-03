// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "hps/hps_metrics.h"

using ::testing::_;
using ::testing::Ge;
using ::testing::Le;

namespace hps {

class HpsMetricsTest : public testing::Test {
 protected:
  HpsMetricsTest() {
    hps_metrics_.SetMetricsLibraryForTesting(
        std::make_unique<MetricsLibraryMock>());
  }
  HpsMetricsTest(const HpsMetricsTest&) = delete;
  HpsMetricsTest& operator=(const HpsMetricsTest&) = delete;

  ~HpsMetricsTest() override = default;

  MetricsLibraryMock* GetMetricsLibraryMock() {
    return static_cast<MetricsLibraryMock*>(
        hps_metrics_.metrics_library_for_testing());
  }

  HpsMetrics hps_metrics_;
};

TEST_F(HpsMetricsTest, SendHpsTurnOnResult) {
  std::vector<HpsTurnOnResult> all_results = {
      HpsTurnOnResult::kSuccess,          HpsTurnOnResult::kMcuVersionMismatch,
      HpsTurnOnResult::kSpiNotVerified,   HpsTurnOnResult::kMcuNotVerified,
      HpsTurnOnResult::kStage1NotStarted, HpsTurnOnResult::kApplNotStarted,
      HpsTurnOnResult::kNoResponse,       HpsTurnOnResult::kTimeout,
      HpsTurnOnResult::kBadMagic,         HpsTurnOnResult::kFault,
  };
  // Check that we have all the values of the enum
  ASSERT_EQ(all_results.size(),
            static_cast<int>(HpsTurnOnResult::kMaxValue) + 1);

  for (auto result : all_results) {
    EXPECT_CALL(*GetMetricsLibraryMock(),
                SendEnumToUMA(kHpsTurnOnResult, static_cast<int>(result), _))
        .Times(1);
    hps_metrics_.SendHpsTurnOnResult(result);
  }
}

}  // namespace hps
