// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/shared_data.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {

TEST(SharedDataTest, TestValidOwnerId) {
  EXPECT_EQ(IsValidOwnerId("abdcefABCDEF0123456789"), true);
}

TEST(SharedDataTest, TestEmptyOwnerId) {
  EXPECT_EQ(IsValidOwnerId(""), false);
}

TEST(SharedDataTest, TestInvalidOwnerId) {
  EXPECT_EQ(IsValidOwnerId("Invalid"), false);
  EXPECT_EQ(IsValidOwnerId("abcd/../012345"), false);
}

TEST(SharedDataTest, TestValidVmName) {
  EXPECT_EQ(IsValidVmName("A Valid VM"), true);
}

TEST(SharedDataTest, TestEmptyVmName) {
  EXPECT_EQ(IsValidVmName(""), false);
}

}  // namespace concierge
}  // namespace vm_tools
