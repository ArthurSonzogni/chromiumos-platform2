// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_tracking.h"

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/version.h>
#include <gtest/gtest.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h>

namespace oobe_config {

namespace {

const base::Version kDeviceVersionM108("15183.1.2");
const char kDeviceVersionM108LsbRelease[] = "15183.1.2";

const base::Version kTargetVersionM107("15117.0.0");
const char kTargetVersionM107PolicyValueExpected[] = "15117.";
const char kTargetVersionM107PolicyValueMajor[] = "15117";
const char kTargetVersionM107PolicyValueMajorWildcard[] = "15117.*";
const char kTargetVersionM107PolicyValueMajorMinorWildcard[] = "15117.2.*";
const char kTargetVersionM107PolicyValueMajorMinorPatch[] = "15117.28.1223";

const base::Version kTargetVersionM106("15509.0.0");
const char kTargetVersionM106PolicyValue[] = "15509.";

const char kTargetVersionInvalidPolicyValue[] = ".";

}  // namespace

class EnterpriseRollbackTrackingTest : public ::testing::Test {
 public:
  void SetUp() override {
    rollback_metrics_ = std::make_unique<
        oobe_config::EnterpriseRollbackMetricsHandlerForTesting>();

    // Enable metrics repoting by default in all tests.
    ASSERT_TRUE(rollback_metrics_->EnableMetrics());
  }

 protected:
  std::unique_ptr<oobe_config::EnterpriseRollbackMetricsHandlerForTesting>
      rollback_metrics_;
  // Need to keep the variable around for the test version to be read.
  base::test::ScopedChromeOSVersionInfo version_info_{
      base::StringPrintf("CHROMEOS_RELEASE_VERSION=%s",
                         kDeviceVersionM108LsbRelease),
      base::Time()};
};

TEST_F(EnterpriseRollbackTrackingTest, VerifyGetDeviceInfo) {
  auto version = GetDeviceVersion();
  ASSERT_TRUE(version.has_value());
  ASSERT_EQ(version.value(), kDeviceVersionM108);
}

TEST_F(EnterpriseRollbackTrackingTest, CleanOutdatedTrackingNoTracking) {
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());

  ASSERT_TRUE(CleanOutdatedTracking(*rollback_metrics_));
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(EnterpriseRollbackTrackingTest,
       CleanOutdatedTrackingWithPreviousTracking) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionM108,
                                                       kTargetVersionM107));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  ASSERT_TRUE(CleanOutdatedTracking(*rollback_metrics_));
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(EnterpriseRollbackTrackingTest,
       IsTrackingForCurrentRollbackTargetVersion) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionM108,
                                                       kTargetVersionM107));

  auto is_tracking_result = IsTrackingForRollbackTargetVersion(
      *rollback_metrics_, kTargetVersionM107PolicyValueExpected);
  ASSERT_TRUE(is_tracking_result.has_value());
  ASSERT_TRUE(is_tracking_result.value());
}

TEST_F(EnterpriseRollbackTrackingTest,
       IsNotTrackingForOutdatedRollbackTargetVersion) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionM108,
                                                       kTargetVersionM107));

  auto is_tracking_result = IsTrackingForRollbackTargetVersion(
      *rollback_metrics_, kTargetVersionM106PolicyValue);
  ASSERT_TRUE(is_tracking_result.has_value());
  ASSERT_FALSE(is_tracking_result.value());
}

TEST_F(EnterpriseRollbackTrackingTest,
       IsNotTrackingRollbackTargetVersionIfNoRollbackTracking) {
  auto is_tracking_result = IsTrackingForRollbackTargetVersion(
      *rollback_metrics_, kTargetVersionM106PolicyValue);
  ASSERT_TRUE(is_tracking_result.has_value());
  ASSERT_FALSE(is_tracking_result.value());
}

TEST_F(EnterpriseRollbackTrackingTest,
       IsTrackingReturnsFalseIfNoTrackingAndNotValidTargetVersionProvided) {
  auto is_tracking_result = IsTrackingForRollbackTargetVersion(
      *rollback_metrics_, kTargetVersionInvalidPolicyValue);
  ASSERT_TRUE(is_tracking_result.has_value());
  ASSERT_FALSE(is_tracking_result.value());
}

TEST_F(EnterpriseRollbackTrackingTest,
       IsTrackingReturnsErrorIfTrackingButNotValidTargetVersionProvided) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionM108,
                                                       kTargetVersionM107));

  auto is_tracking_result = IsTrackingForRollbackTargetVersion(
      *rollback_metrics_, kTargetVersionInvalidPolicyValue);
  ASSERT_FALSE(is_tracking_result.has_value());
  ASSERT_EQ(is_tracking_result.error(),
            "Error converting target version policy");
}

TEST_F(EnterpriseRollbackTrackingTest,
       DoNotStartTrackingRollbackIfMetricsAreDisabled) {
  ASSERT_TRUE(rollback_metrics_->DisableMetrics());

  ASSERT_FALSE(StartNewTracking(*rollback_metrics_,
                                kTargetVersionM107PolicyValueExpected));

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
  ASSERT_EQ(0, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithExpectedPolicyValueStartsTracking) {
  ASSERT_TRUE(StartNewTracking(*rollback_metrics_,
                               kTargetVersionM107PolicyValueExpected));

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForTargetVersion(kTargetVersionM107));
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForDeviceVersion(kDeviceVersionM108));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithInvalidPolicyValueDoesNotStartTracking) {
  ASSERT_FALSE(
      StartNewTracking(*rollback_metrics_, kTargetVersionInvalidPolicyValue));
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
  ASSERT_EQ(0, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithMajorPolicyValueTracksMajor) {
  ASSERT_TRUE(
      StartNewTracking(*rollback_metrics_, kTargetVersionM107PolicyValueMajor));

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForTargetVersion(kTargetVersionM107));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithWithMajorWildcardPolicyValueTracksMajor) {
  ASSERT_TRUE(StartNewTracking(*rollback_metrics_,
                               kTargetVersionM107PolicyValueMajorWildcard));

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForTargetVersion(kTargetVersionM107));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithMajorMinorWildcardPolicyValueTracksMajor) {
  ASSERT_TRUE(StartNewTracking(
      *rollback_metrics_, kTargetVersionM107PolicyValueMajorMinorWildcard));

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForTargetVersion(kTargetVersionM107));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(EnterpriseRollbackTrackingTest,
       StartNewTrackingWithMajorMinorPatchPolicyValueTracksMajor) {
  ASSERT_TRUE(StartNewTracking(*rollback_metrics_,
                               kTargetVersionM107PolicyValueMajorMinorPatch));

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(
      rollback_metrics_->IsTrackingForTargetVersion(kTargetVersionM107));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

}  // namespace oobe_config
