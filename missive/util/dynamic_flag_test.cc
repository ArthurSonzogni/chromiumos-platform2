// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/dynamic_flag.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace reporting {
namespace {

class TestFlagDerived : public DynamicFlag {
 public:
  explicit TestFlagDerived(bool is_enabled)
      : DynamicFlag("test_flag", is_enabled) {}
};

class TestFlagAggregated {
 public:
  explicit TestFlagAggregated(bool is_enabled)
      : flag_("test_flag", is_enabled) {}

  bool is_enabled() const { return flag_.is_enabled(); }

  void OnEnableUpdate(bool is_enabled) { flag_.OnEnableUpdate(is_enabled); }

 private:
  DynamicFlag flag_;
};

TEST(DerivedFlagTest, OnAndOff) {
  TestFlagDerived flag(/*is_enabled=*/true);
  EXPECT_TRUE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/true);  // same
  EXPECT_TRUE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/false);  // flip
  EXPECT_FALSE(flag.is_enabled());
}

TEST(DerivedFlagTest, OffAndOn) {
  TestFlagDerived flag(/*is_enabled=*/false);
  EXPECT_FALSE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/false);  // same
  EXPECT_FALSE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/true);  // flip
  EXPECT_TRUE(flag.is_enabled());
}

TEST(AggregatedFlagTest, OnAndOff) {
  TestFlagAggregated flag(/*is_enabled=*/true);
  EXPECT_TRUE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/true);  // same
  EXPECT_TRUE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/false);  // flip
  EXPECT_FALSE(flag.is_enabled());
}

TEST(AggregatedFlagTest, OffAndOn) {
  TestFlagAggregated flag(/*is_enabled=*/false);
  EXPECT_FALSE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/false);  // same
  EXPECT_FALSE(flag.is_enabled());
  flag.OnEnableUpdate(/*is_enabled=*/true);  // flip
  EXPECT_TRUE(flag.is_enabled());
}
}  // namespace
}  // namespace reporting
