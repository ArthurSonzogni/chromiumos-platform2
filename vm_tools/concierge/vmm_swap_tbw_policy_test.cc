// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

#include <base/time/time.h>
#include <gtest/gtest.h>

namespace vm_tools::concierge {

TEST(VmmSwapTbwPolicyTest, CanSwapOut) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutWithin1dayTarget) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(399);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTarget) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(400);

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTargetWithMultiRecords) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  // Buffer size is 28 but they are merged within 1 day.
  for (int i = 0; i < 100; i++) {
    policy.Record(4);
  }

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds1dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(400, now - base::Days(1));

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 6; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }
  policy.Record(199, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(7 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 27; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }
  policy.Record(99, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(28 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST(VmmSwapTbwPolicyTest, CanSwapOutIgnoreRotatedObsoleteData) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(400, now - base::Days(56 - i));
  }
  policy.Record(399, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

}  // namespace vm_tools::concierge
