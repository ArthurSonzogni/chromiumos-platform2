// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fusebox_helper.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check_op.h>
#include <base/strings/string_util.h>
#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mock_platform.h"
#include "cros-disks/uri.h"

namespace cros_disks {

namespace {

using testing::ElementsAre;

const base::FilePath kMountDir("/mount-dir");
const Uri kFuseBoxSource("fusebox", "source");

const char kOwnerUserName[] = "fuse-fusebox";
const uid_t kFuseBoxUserUID = 312;
const gid_t kFuseBoxUserGID = 312;

}  // namespace

class FuseBoxHelperTest : public ::testing::Test {
 public:
  FuseBoxHelperTest() : helper_(&platform_, &process_reaper_) {}

 protected:
  std::vector<std::string> ConfigureSandbox(const std::string& source,
                                            std::vector<std::string> options) {
    FakeSandboxedProcess sandbox;
    MountError error = helper_.ConfigureSandbox(source, kMountDir,
                                                std::move(options), &sandbox);
    EXPECT_EQ(error, MountError::kSuccess);
    return sandbox.arguments();
  }

  class MockPlatformForTesting : public MockPlatform {
   public:
    bool GetUserAndGroupId(const std::string& name,
                           uid_t* uid,
                           gid_t* gid) const override {
      if (name != kOwnerUserName)
        return false;
      if (uid)
        *uid = kFuseBoxUserUID;
      if (gid)
        *gid = kFuseBoxUserGID;
      return true;
    }
  };

 protected:
  brillo::ProcessReaper process_reaper_;
  MockPlatformForTesting platform_;
  FuseBoxHelper helper_;
};

TEST_F(FuseBoxHelperTest, SourceUri) {
  auto source = kFuseBoxSource.value();
  EXPECT_EQ("fusebox://source", source);
}

TEST_F(FuseBoxHelperTest, CreateMounter) {
  EXPECT_THAT(ConfigureSandbox(kFuseBoxSource.value(), {}),
              ElementsAre("-o", "uid=1000,gid=1001"));
}

TEST_F(FuseBoxHelperTest, CreateMounterWithOptions) {
  EXPECT_THAT(
      ConfigureSandbox(kFuseBoxSource.value(),
                       {"--test", "--ll=max_read=131072,max_background=3"}),
      ElementsAre("-o", "uid=1000,gid=1001"));
}

TEST_F(FuseBoxHelperTest, CreateMounterWithReadOnlyMountOption) {
  EXPECT_THAT(ConfigureSandbox(kFuseBoxSource.value(), {"--test", "ro"}),
              ElementsAre("-o", "ro", "-o", "uid=1000,gid=1001"));
}

TEST_F(FuseBoxHelperTest, CreateMounterWithReadWriteMountOption) {
  EXPECT_THAT(ConfigureSandbox(kFuseBoxSource.value(), {"--test", "rw"}),
              ElementsAre("-o", "rw", "-o", "uid=1000,gid=1001"));
}

TEST_F(FuseBoxHelperTest, CanMount) {
  base::FilePath name;
  EXPECT_TRUE(helper_.CanMount("fusebox://", {}, &name));
  EXPECT_EQ("fusebox", name.value());
  EXPECT_TRUE(helper_.CanMount("fusebox://foobar", {}, &name));
  EXPECT_EQ("foobar", name.value());
  EXPECT_TRUE(helper_.CanMount("fusebox://foo/bar", {}, &name));
  EXPECT_EQ("foo/bar", name.value());

  base::FilePath other;
  EXPECT_FALSE(helper_.CanMount("otherfs://foo", {}, &other));
  EXPECT_TRUE(other.value().empty());
  EXPECT_FALSE(helper_.CanMount("otherfs://", {}, &other));
  EXPECT_TRUE(other.value().empty());
}

TEST_F(FuseBoxHelperTest, OwnerUser) {
  OwnerUser user = helper_.ResolveFuseBoxOwnerUser(&platform_);
  EXPECT_EQ(kFuseBoxUserUID, user.uid);
  EXPECT_EQ(kFuseBoxUserGID, user.gid);
}

}  // namespace cros_disks
