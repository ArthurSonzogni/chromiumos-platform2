// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/cpu_limiter.h"

#include <gtest/gtest.h>

namespace chromeos_update_engine {

class CPULimiterTest : public ::testing::Test {};

namespace {
// Compares cpu shares and returns an integer that is less
// than, equal to or greater than 0 if |shares_lhs| is,
// respectively, lower than, same as or higher than |shares_rhs|.
int CompareCpuShares(CpuShares shares_lhs, CpuShares shares_rhs) {
  return static_cast<int>(shares_lhs) - static_cast<int>(shares_rhs);
}
}  // namespace

// Tests the CPU shares enum is in the order we expect it.
TEST(CPULimiterTest, CompareCpuSharesTest) {
  EXPECT_LT(CompareCpuShares(CpuShares::kLow, CpuShares::kNormal), 0);
  EXPECT_GT(CompareCpuShares(CpuShares::kNormal, CpuShares::kLow), 0);
  EXPECT_EQ(CompareCpuShares(CpuShares::kNormal, CpuShares::kNormal), 0);
  EXPECT_GT(CompareCpuShares(CpuShares::kHigh, CpuShares::kNormal), 0);
}

}  // namespace chromeos_update_engine
