// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/feature_util.h"

#include <gtest/gtest.h>

namespace vm_tools::concierge {

TEST(FeatureUtilTest, TestEmptyMapReturnsNull) {
  ASSERT_FALSE(FindIntValue({}, "TestKey"));
}

TEST(FeatureUtilTest, TestMissingKeyReturnsNull) {
  ASSERT_FALSE(FindIntValue({{"TestParam", "4"}, {"AnotherTestParam", "5"}},
                            "NotATestParam"));
}

TEST(FeatureUtilTest, TestNonIntValueReturnsNull) {
  ASSERT_FALSE(FindIntValue(
      {{"TestParam", "4"}, {"AnotherTestParam", "ThisIsNotAnInteger"}},
      "AnotherTestParam"));
}

TEST(FeatureUtilTest, TestParseSuccess) {
  auto res = FindIntValue({{"TestParam", "4"}, {"AnotherTestParam", "3"}},
                          "TestParam");
  ASSERT_TRUE(res);
  ASSERT_EQ(*res, 4);
}

}  // namespace vm_tools::concierge
