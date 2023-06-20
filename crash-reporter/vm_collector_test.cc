// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/vm_collector.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class VmCollectorTest : public ::testing::Test {
 protected:
  VmCollector collector_;
};

TEST_F(VmCollectorTest, ComputeSeverity) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("any executable");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kError);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}
