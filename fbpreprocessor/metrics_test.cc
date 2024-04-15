// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <gtest/gtest.h>
#include <metrics/fake_metrics_library.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/metrics.h"

namespace fbpreprocessor {
namespace {

class MetricsTest : public testing::Test {
 protected:
  void SetUp() override {
    auto uma_lib = std::make_unique<FakeMetricsLibrary>();
    uma_lib_ = uma_lib.get();
    metrics_.SetLibraryForTesting(std::move(uma_lib));
  }

  std::vector<int> GetMetricCalls(const std::string& name) const {
    CHECK(uma_lib_);
    return uma_lib_->GetCalls(name);
  }

  Metrics metrics_;

 private:
  // Owned by |fbpreprocessor::Metrics|.
  FakeMetricsLibrary* uma_lib_;
};

TEST_F(MetricsTest, SendNumberOfWiFiDumps) {
  std::vector<int> expected_calls{4, 2, 3, 1};
  bool success = true;

  for (auto num : expected_calls) {
    success = success && metrics_.SendNumberOfAvailableDumps(
                             FirmwareDump::Type::kWiFi, num);
  }
  EXPECT_TRUE(success);
  EXPECT_EQ(GetMetricCalls("Platform.FbPreprocessor.WiFi.Output.Number"),
            expected_calls);
}

TEST_F(MetricsTest, SendWiFiAllowedStatus) {
  // Use integer values in |expected_calls| to ensure that those aren't changed
  // by accident since that would break the interpretation of the metric
  // server-side.
  std::vector<int> expected_calls{0, 1, 2, 3, 4, 5};
  bool success = true;

  Metrics::CollectionAllowedStatus status =
      Metrics::CollectionAllowedStatus::kUnknown;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);
  status = Metrics::CollectionAllowedStatus::kAllowed;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);
  status = Metrics::CollectionAllowedStatus::kDisallowedByPolicy;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);
  status = Metrics::CollectionAllowedStatus::kDisallowedByFinch;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);
  status = Metrics::CollectionAllowedStatus::kDisallowedForMultipleSessions;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);
  status = Metrics::CollectionAllowedStatus::kDisallowedForUserDomain;
  success =
      success && metrics_.SendAllowedStatus(FirmwareDump::Type::kWiFi, status);

  EXPECT_TRUE(success);
  EXPECT_EQ(GetMetricCalls("Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            expected_calls);
}

TEST_F(MetricsTest, SendPseudonymizationType) {
  // Use integer values in |expected_calls| to ensure that those aren't changed
  // by accident since that would break the interpretation of the metric
  // server-side.
  std::vector<int> expected_calls{1, 1};
  bool success = true;

  success = success && metrics_.SendPseudonymizationFirmwareType(
                           FirmwareDump::Type::kWiFi);
  success = success && metrics_.SendPseudonymizationFirmwareType(
                           FirmwareDump::Type::kWiFi);

  EXPECT_TRUE(success);
  EXPECT_EQ(GetMetricCalls("Platform.FbPreprocessor.Pseudonymization.DumpType"),
            expected_calls);
}

TEST_F(MetricsTest, SendWiFiPseudonymizationResult) {
  // Use integer values in |expected_calls| to ensure that those aren't changed
  // by accident since that would break the interpretation of the metric
  // server-side.
  std::vector<int> expected_calls{0, 1, 2, 3, 2, 1, 0};
  bool success = true;

  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kUnknown);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kSuccess);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kFailedToStart);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kNoOpFailedToMove);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kFailedToStart);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kSuccess);
  success = success && metrics_.SendPseudonymizationResult(
                           FirmwareDump::Type::kWiFi,
                           Metrics::PseudonymizationResult::kUnknown);

  EXPECT_TRUE(success);
  EXPECT_EQ(
      GetMetricCalls("Platform.FbPreprocessor.WiFi.Pseudonymization.Result"),
      expected_calls);
}

}  // namespace
}  // namespace fbpreprocessor
