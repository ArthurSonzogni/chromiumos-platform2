// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

#include <string>

#include <gtest/gtest.h>

namespace arc {

class ArcVmDataMigrationHelperDelegateTest : public ::testing::Test {
 public:
  ArcVmDataMigrationHelperDelegateTest() = default;
  virtual ~ArcVmDataMigrationHelperDelegateTest() = default;

  ArcVmDataMigrationHelperDelegateTest(
      const ArcVmDataMigrationHelperDelegateTest&) = delete;
  ArcVmDataMigrationHelperDelegateTest& operator=(
      const ArcVmDataMigrationHelperDelegateTest&) = delete;
};

TEST_F(ArcVmDataMigrationHelperDelegateTest, ConvertXattrName) {
  ArcVmDataMigrationHelperDelegate delegate;

  // user.virtiofs.security.* is converted to security.*.
  EXPECT_EQ(delegate.ConvertXattrName("user.virtiofs.security.sehash"),
            "security.sehash");
  // Other xattrs are kept as-is.
  EXPECT_EQ(delegate.ConvertXattrName("security.selinux"), "security.selinux");
  EXPECT_EQ(delegate.ConvertXattrName("user.attr"), "user.attr");
  EXPECT_EQ(delegate.ConvertXattrName("system.attr"), "system.attr");
  EXPECT_EQ(delegate.ConvertXattrName("trusted.attr"), "trusted.attr");
}

}  // namespace arc
