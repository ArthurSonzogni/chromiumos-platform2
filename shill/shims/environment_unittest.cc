// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shims/environment.h"

#include <base/stl_util.h>
#include <gtest/gtest.h>

using std::map;
using std::string;

namespace shill {

namespace shims {

class EnvironmentTest : public testing::Test {
 public:
  EnvironmentTest() : environment_(Environment::GetInstance()) {}

 protected:
  Environment *environment_;
};

TEST_F(EnvironmentTest, GetVariable) {
  static const char * const kVarValues[] = {
    "VALUE",
    "",
  };
  static const char kVarName[] = "SHILL_SHIMS_GET_VARIABLE_TEST";
  for (size_t i = 0; i < arraysize(kVarValues); i++) {
    EXPECT_FALSE(environment_->GetVariable(kVarName, NULL));
    EXPECT_EQ(0, setenv(kVarName, kVarValues[i], 0)) << kVarValues[i];
    string value;
    EXPECT_TRUE(environment_->GetVariable(kVarName, &value)) << kVarValues[i];
    EXPECT_EQ(kVarValues[i], value);
    EXPECT_EQ(0, unsetenv(kVarName));
  }
}

TEST_F(EnvironmentTest, AsMap) {
  static const char * const kVarNames[] = {
    "SHILL_SHIMS_AS_MAP_TEST_1",
    "SHILL_SHIMS_AS_MAP_TEST_EMPTY",
    "SHILL_SHIMS_AS_MAP_TEST_2",
  };
  static const char * const kVarValues[] = {
    "VALUE 1",
    "",
    "VALUE 2",
  };
  ASSERT_EQ(arraysize(kVarNames), arraysize(kVarValues));
  for (size_t i = 0; i < arraysize(kVarNames); i++) {
    EXPECT_EQ(0, setenv(kVarNames[i], kVarValues[i], 0)) << kVarNames[i];
  }
  map<string, string> env = environment_->AsMap();
  for (size_t i = 0; i < arraysize(kVarNames); i++) {
    EXPECT_TRUE(ContainsKey(env, kVarNames[i])) << kVarNames[i];
    EXPECT_EQ(kVarValues[i], env[kVarNames[i]]) << kVarNames[i];
    EXPECT_EQ(0, unsetenv(kVarNames[i])) << kVarNames[i];
  }
}

}  // namespace shims

}  // namespace shill
