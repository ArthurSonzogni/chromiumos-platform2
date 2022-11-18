// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/metrics_sender.h"

#include <memory>

#include "gtest/gtest.h"
#include "metrics/metrics_library_mock.h"

namespace secagentd::testing {

using ::testing::Return;

class MetricsSenderTestFixture : public ::testing::Test {
 protected:
  MetricsSenderTestFixture() {
    auto metrics_library_mock = std::make_unique<MetricsLibraryMock>();
    metrics_library_mock_ = metrics_library_mock.get();
    metrics_sender_ =
        MetricsSender::CreateForTesting(std::move(metrics_library_mock));
  }
  MetricsLibraryMock* metrics_library_mock_;
  std::unique_ptr<MetricsSender> metrics_sender_;
};

TEST_F(MetricsSenderTestFixture, SendEnumMetricToUMA) {
  enum class TestEnum {
    kZero,
    kOne,
    kTwo,
    kMaxValue = kTwo,
  };
  const metrics::EnumMetric<TestEnum> kTestMetric = {.name = "TestMetric"};
  EXPECT_CALL(*metrics_library_mock_,
              SendEnumToUMA("ChromeOS.Secagentd.TestMetric",
                            static_cast<int>(TestEnum::kOne),
                            static_cast<int>(TestEnum::kMaxValue) + 1))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      metrics_sender_->SendEnumMetricToUMA(kTestMetric, TestEnum::kOne));
}
}  // namespace secagentd::testing
