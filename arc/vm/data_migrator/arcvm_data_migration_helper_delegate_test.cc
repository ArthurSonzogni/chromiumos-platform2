// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace arc {

namespace {

constexpr uid_t kAndroidRootUid = 655360;
constexpr gid_t kAndroidRootGid = 655360;

}  // namespace

class ArcVmDataMigrationHelperDelegateTest : public ::testing::Test {
 public:
  ArcVmDataMigrationHelperDelegateTest() = default;
  virtual ~ArcVmDataMigrationHelperDelegateTest() = default;

  ArcVmDataMigrationHelperDelegateTest(
      const ArcVmDataMigrationHelperDelegateTest&) = delete;
  ArcVmDataMigrationHelperDelegateTest& operator=(
      const ArcVmDataMigrationHelperDelegateTest&) = delete;
};

TEST_F(ArcVmDataMigrationHelperDelegateTest, ConvertUid) {
  ArcVmDataMigrationHelperDelegate delegate;

  // Valid host-to-guest UID mappings (pairs of (host UID, guest UID)).
  std::vector<std::pair<uid_t, uid_t>> mapping_test_cases = {{
      // [655360, 660360) is mapped to [0, 5000).
      {655360, 0},     // AID_ROOT
      {656360, 1000},  // AID_SYSTEM
      {657360, 2000},  // AID_SHELL (adb)
      {660359, 4999},

      // [600, 650) is mapped to [5000, 5050).
      {600, 5000},
      {602, 5002},  // arc-bridge
      {649, 5049},

      // [660410, 2655360) is mapped to [5050, 2000000).
      {660410, 5050},
  }};

  // Host UIDs that will not be mapped to a valid guest UID.
  std::vector<uid_t> out_of_range_host_uids = {0, 650, 1000, 660360};

  for (const auto& [host_uid, guest_uid] : mapping_test_cases) {
    base::stat_wrapper_t stat;
    stat.st_uid = host_uid;
    stat.st_gid = kAndroidRootGid;  // Avoid warning spams for invalid GID.
    EXPECT_TRUE(delegate.ConvertFileMetadata(&stat));
    EXPECT_EQ(stat.st_uid, guest_uid);
  }

  for (const auto& host_uid : out_of_range_host_uids) {
    base::stat_wrapper_t stat;
    stat.st_uid = host_uid;
    stat.st_gid = kAndroidRootGid;  // Avoid warning spams for invalid GID.
    EXPECT_FALSE(delegate.ConvertFileMetadata(&stat));
  }
}

TEST_F(ArcVmDataMigrationHelperDelegateTest, ConvertGid) {
  ArcVmDataMigrationHelperDelegate delegate;

  // Valid host-to-guest GID mappings (pairs of (host GID, guest GID)).
  std::vector<std::pair<gid_t, gid_t>> mapping_test_cases = {{
      // [655360, 656425) is mapped to [0, 1065).
      {655360, 0},     // AID_ROOT
      {656360, 1000},  // AID_SYSTEM
      {656424, 1064},

      // 20119 (android-reserved-disk) is mapped to 1065 (AID_RESERVED_DISK).
      {20119, 1065},

      // [656426, 660360) is mapped to [1066, 5000).
      {656426, 1066},
      {657360, 2000},  // AID_SHELL (adb)
      {660359, 4999},

      // [600, 650) is mapped to [5000, 5050).
      {600, 5000},
      {602, 5002},  // arc-bridge
      {649, 5049},

      // [660410, 2655360) is mapped to [5050, 2000000).
      {660410, 5050},
  }};

  // Host GIDs that will not be mapped to a valid guest GID.
  std::vector<gid_t> out_of_range_host_gids = {0, 650, 1000, 656425, 660360};

  for (const auto& [host_gid, guest_gid] : mapping_test_cases) {
    base::stat_wrapper_t stat;
    stat.st_gid = host_gid;
    stat.st_uid = kAndroidRootUid;  // Avoid warning spams for invalid UID.
    EXPECT_TRUE(delegate.ConvertFileMetadata(&stat));
    EXPECT_EQ(stat.st_gid, guest_gid);
  }

  for (const auto& host_gid : out_of_range_host_gids) {
    base::stat_wrapper_t stat;
    stat.st_gid = host_gid;
    stat.st_uid = kAndroidRootUid;  // Avoid warning spams for invalid UID.
    EXPECT_FALSE(delegate.ConvertFileMetadata(&stat));
  }
}

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
