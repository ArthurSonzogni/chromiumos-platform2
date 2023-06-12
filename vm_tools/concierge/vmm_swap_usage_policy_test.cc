// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_usage_policy.h"

#include <base/time/time.h>
#include <gtest/gtest.h>

namespace vm_tools::concierge {

TEST(VmmSwapUsagePolicyTest, PredictDuration) {
  VmmSwapUsagePolicy policy;

  EXPECT_TRUE(policy.PredictDuration().is_zero());
}

TEST(VmmSwapUsagePolicyTest, PredictDurationJustLogLongTimeAgo) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));
  policy.OnDisabled(now - base::Days(28) - base::Seconds(1));

  EXPECT_TRUE(policy.PredictDuration(now).is_zero());
}

TEST(VmmSwapUsagePolicyTest, PredictDurationEnabledFullTime) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));

  EXPECT_EQ(policy.PredictDuration(now), base::Days(28 + 21 + 14 + 7) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationWithMissingEnabledRecord) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));
  policy.OnDisabled(now - base::Days(29) + base::Minutes(50));
  // This enabled record is skipped.
  policy.OnEnabled(now - base::Days(29) + base::Minutes(30));

  EXPECT_EQ(policy.PredictDuration(now), base::Days(28 + 21 + 14 + 7) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLessThan1WeekDataWhileDisabled) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(7) + base::Hours(1));
  policy.OnDisabled(now - base::Days(7) + base::Hours(10));

  policy.OnEnabled(now - base::Days(6));
  policy.OnDisabled(now - base::Days(6) + base::Hours(1));

  // The latest enabled duration * 2
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(2));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLessThan1WeekDataWhileEnabled) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(6));
  policy.OnDisabled(now - base::Days(6) + base::Hours(1));

  policy.OnEnabled(now - base::Minutes(10));

  // The latest enabled duration * 2
  EXPECT_EQ(policy.PredictDuration(now), base::Minutes(20));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationJust1WeekData) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(7));
  policy.OnDisabled(now - base::Days(7) + base::Hours(10));

  policy.OnEnabled(now - base::Days(6));

  // The latest enabled duration
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(10));
}

TEST(VmmSwapUsagePolicyTest,
     PredictDurationLessThan1WeekDataWhileMultipleEnabled) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Minutes(50));
  policy.OnDisabled(now - base::Minutes(30));
  policy.OnEnabled(now - base::Minutes(5));

  // The latest enabled duration in 1 hour * 2.
  EXPECT_EQ(policy.PredictDuration(now), base::Minutes(40));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLessThan2WeekData) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(10));
  policy.OnDisabled(now - base::Days(8));
  // Enabled record across the point 1 week ago.
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(1));
  policy.OnEnabled(now - base::Minutes(30));

  EXPECT_EQ(policy.PredictDuration(now), base::Hours(1));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLessThan3WeekData) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(5));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLessThan4WeekData) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(2));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 2 + 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(4));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationFullData) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(16));
  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(2));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 16 + 2 + 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(7));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationFullDataWithEmptyWeeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(16));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 16 + 0 + 0 + 0 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(4));
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLong2WeeksData4Weeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (7days + 3hours) + 3hours + 0 + 6hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(12)) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLong3WeeksData4Weeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (14days + 3hours) + (7days+3hours) + 3hours + 6hours.
  EXPECT_EQ(policy.PredictDuration(now),
            (base::Days(21) + base::Hours(15)) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLong4WeeksData4Weeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(3));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (21days + 3hours) + (14days+3hours) + (7days+3hours) + 3hours.
  EXPECT_EQ(policy.PredictDuration(now),
            (base::Days(42) + base::Hours(12)) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLongData3Weeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(3));
  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of  3hours + (7days + 3hours) + 3hours + 6hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(15)) / 4);
}

TEST(VmmSwapUsagePolicyTest, PredictDurationLongData2Weeks) {
  VmmSwapUsagePolicy policy;
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(3));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(3));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of  3hours + 0 + (7days + 3hours) + 3hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(9)) / 4);
}

}  // namespace vm_tools::concierge
