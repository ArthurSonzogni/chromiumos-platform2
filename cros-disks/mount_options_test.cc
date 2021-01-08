// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <sys/mount.h>

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cros_disks {

using testing::ElementsAre;

TEST(MountOptionsTest, IsReadOnlyMount) {
  EXPECT_FALSE(IsReadOnlyMount({}));
  EXPECT_FALSE(IsReadOnlyMount({"foo", "bar"}));
  EXPECT_TRUE(IsReadOnlyMount({"ro"}));
  EXPECT_FALSE(IsReadOnlyMount({"ro", "rw"}));
  EXPECT_TRUE(IsReadOnlyMount({"foo", "ro", "bar", "rw", "ro", "baz"}));
}

TEST(MountOptionsTest, GetParamValue) {
  std::string value;
  EXPECT_FALSE(GetParamValue({}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"a=b", "foo=bar", "baz", "x=y"}, "foo", &value));
  EXPECT_EQ("bar", value);
  EXPECT_FALSE(GetParamValue({"foo"}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"foo=bar", "foo=baz"}, "foo", &value));
  EXPECT_EQ("baz", value);
}

TEST(MountOptionsTest, SetParamValue) {
  std::vector<std::string> params;
  SetParamValue(&params, "foo", "bar");
  SetParamValue(&params, "baz", "");
  EXPECT_THAT(params, ElementsAre("foo=bar", "baz="));
}

TEST(MountOptionsTest, HasExactParam) {
  EXPECT_TRUE(HasExactParam({"abc", "foo", "bar=baz"}, "foo"));
  EXPECT_FALSE(HasExactParam({"abc", "foo", "bar=baz"}, "bar"));
}

TEST(MountOptionsTest, RemoveParamsEqualTo) {
  std::vector<std::string> params = {"abc", "foo", "bar=baz", "abc"};
  EXPECT_EQ(0, RemoveParamsEqualTo(&params, "bar"));
  EXPECT_THAT(params, ElementsAre("abc", "foo", "bar=baz", "abc"));
  EXPECT_EQ(1, RemoveParamsEqualTo(&params, "foo"));
  EXPECT_THAT(params, ElementsAre("abc", "bar=baz", "abc"));
  EXPECT_EQ(2, RemoveParamsEqualTo(&params, "abc"));
  EXPECT_THAT(params, ElementsAre("bar=baz"));
}

TEST(MountOptionsTest, RemoveParamsWithSameName) {
  std::vector<std::string> params = {"abc", "foo=0", "bar=baz", "foo=1"};
  EXPECT_EQ(0, RemoveParamsWithSameName(&params, "abc"));
  EXPECT_THAT(params, ElementsAre("abc", "foo=0", "bar=baz", "foo=1"));
  EXPECT_EQ(1, RemoveParamsWithSameName(&params, "bar"));
  EXPECT_THAT(params, ElementsAre("abc", "foo=0", "foo=1"));
  EXPECT_EQ(2, RemoveParamsWithSameName(&params, "foo"));
  EXPECT_THAT(params, ElementsAre("abc"));
}

}  // namespace cros_disks
