// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/excluder_chromeos.h"

#include <gtest/gtest.h>

#include "update_engine/cros/fake_system_state.h"

namespace chromeos_update_engine {

namespace {
constexpr char kFakeHash[] =
    "71ff43d76e2488e394e46872f5b066cc25e394c2c3e3790dd319517883b33db1";
}  // namespace

class ExcluderChromeOSTest : public ::testing::Test {
 protected:
  void SetUp() override { FakeSystemState::CreateInstance(); }

  ExcluderChromeOS excluder_;
};

TEST_F(ExcluderChromeOSTest, ExclusionCheck) {
  EXPECT_FALSE(excluder_.IsExcluded(kFakeHash));
  EXPECT_TRUE(excluder_.Exclude(kFakeHash));
  EXPECT_TRUE(excluder_.IsExcluded(kFakeHash));
}

TEST_F(ExcluderChromeOSTest, ResetFlow) {
  EXPECT_TRUE(excluder_.Exclude("abc"));
  EXPECT_TRUE(excluder_.Exclude(kFakeHash));
  EXPECT_TRUE(excluder_.IsExcluded("abc"));
  EXPECT_TRUE(excluder_.IsExcluded(kFakeHash));

  EXPECT_TRUE(excluder_.Reset());
  EXPECT_FALSE(excluder_.IsExcluded("abc"));
  EXPECT_FALSE(excluder_.IsExcluded(kFakeHash));
}

}  // namespace chromeos_update_engine
