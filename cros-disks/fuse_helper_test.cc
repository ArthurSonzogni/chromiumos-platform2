// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_helper.h"

#include <string>
#include <vector>

#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/uri.h"

using base::FilePath;
using std::string;
using std::vector;

namespace cros_disks {

namespace {

const char kFUSEType[] = "fuse";
const char kMountProgram[] = "dummy";
const char kMountUser[] = "nobody";
const Uri kSomeUri("fuse", "some/src/path");
const FilePath kWorkingDir("/wkdir");
const FilePath kMountDir("/mnt");

}  // namespace

class FUSEHelperTest : public ::testing::Test {
 public:
  FUSEHelperTest()
      : helper_(kFUSEType, &platform_, FilePath(kMountProgram), kMountUser) {}

 protected:
  Platform platform_;
  FUSEHelper helper_;
};

// Verifies that CanMount correctly identifies handleable URIs.
TEST_F(FUSEHelperTest, CanMount) {
  EXPECT_TRUE(helper_.CanMount(Uri::Parse("fuse://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("boose://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("http://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("fuse://")));
}

// Verifies that GetTargetSuffix escapes unwanted chars in URI.
TEST_F(FUSEHelperTest, GetTargetSuffix) {
  EXPECT_EQ("foo", helper_.GetTargetSuffix(Uri::Parse("fuse://foo")));
  EXPECT_EQ("", helper_.GetTargetSuffix(Uri::Parse("fuse://")));
  EXPECT_EQ("a:b@c:d$__$etc$",
            helper_.GetTargetSuffix(Uri::Parse("fuse://a:b@c:d/../etc/")));
}

// Verifies that generic implementation applies default rules to MountOptions.
TEST_F(FUSEHelperTest, PrepareMountOptions) {
  vector<string> options = {"bind", "foo=bar", "baz", "dirsync"};
  auto mounter =
      helper_.CreateMounter(kWorkingDir, kSomeUri, kMountDir, options);
  EXPECT_EQ(kFUSEType, mounter->filesystem_type());
  EXPECT_EQ(kSomeUri.path(), mounter->source_path());
  EXPECT_EQ(kMountDir.value(), mounter->target_path());
  string opts = mounter->mount_options().ToString();
  EXPECT_THAT(opts, testing::StartsWith("bind,dirsync,"));
  EXPECT_THAT(opts, testing::Not(testing::HasSubstr("uid=")));
}

}  // namespace cros_disks
