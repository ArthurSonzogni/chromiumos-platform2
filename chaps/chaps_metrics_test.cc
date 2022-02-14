// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/chaps_metrics.h"

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

namespace chaps {

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

}  // namespace

class ChapsMetricsTest : public ::testing::Test {
 public:
  ChapsMetricsTest() {
    chaps_metrics_.set_metrics_library_for_testing(&mock_metrics_library_);
  }

 protected:
  StrictMock<MetricsLibraryMock> mock_metrics_library_;
  ChapsMetrics chaps_metrics_;
};

TEST_F(ChapsMetricsTest, ReportReinitializingTokenStatus) {
  // Tests the enums to see if the parameters are correctly passed.
  const ReinitializingTokenStatus statuses[]{
      ReinitializingTokenStatus::kFailedToUnseal,
      ReinitializingTokenStatus::kBadAuthorizationData,
      ReinitializingTokenStatus::kFailedToDecryptRootKey,
      ReinitializingTokenStatus::kFailedToValidate,
  };
  constexpr auto max_value =
      static_cast<int>(ReinitializingTokenStatus::kMaxValue);
  for (auto status : statuses) {
    EXPECT_CALL(mock_metrics_library_,
                SendEnumToUMA(kReinitializingToken, static_cast<int>(status),
                              max_value))
        .WillOnce(Return(true));
    chaps_metrics_.ReportReinitializingTokenStatus(status);
  }
}

TEST_F(ChapsMetricsTest, ReportTPMAvailabilityStatus) {
  // Tests the enums to see if the parameters are correctly passed.
  const TPMAvailabilityStatus statuses[]{
      TPMAvailabilityStatus::kTPMAvailable,
      TPMAvailabilityStatus::kTPMUnavailable,
  };
  constexpr auto max_value = static_cast<int>(TPMAvailabilityStatus::kMaxValue);
  for (auto status : statuses) {
    EXPECT_CALL(
        mock_metrics_library_,
        SendEnumToUMA(kTPMAvailability, static_cast<int>(status), max_value))
        .WillOnce(Return(true));
    chaps_metrics_.ReportTPMAvailabilityStatus(status);
  }
}

TEST_F(ChapsMetricsTest, ReportCrosEvent) {
  EXPECT_CALL(mock_metrics_library_, SendCrosEventToUMA(kDatabaseCorrupted))
      .WillOnce(Return(true));
  chaps_metrics_.ReportCrosEvent(kDatabaseCorrupted);

  EXPECT_CALL(mock_metrics_library_, SendCrosEventToUMA(kDatabaseRepairFailure))
      .WillOnce(Return(true));
  chaps_metrics_.ReportCrosEvent(kDatabaseRepairFailure);

  EXPECT_CALL(mock_metrics_library_, SendCrosEventToUMA(kDatabaseCreateFailure))
      .WillOnce(Return(true));
  chaps_metrics_.ReportCrosEvent(kDatabaseCreateFailure);
}

}  // namespace chaps
