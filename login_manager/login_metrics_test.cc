// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/login_metrics.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace login_manager {

struct UserTypeTestParams {
  UserTypeTestParams(LoginMetrics::UserType t, bool d, bool g, bool o)
      : expected_type(t), dev_mode(d), guest(g), owner(o) {}
  LoginMetrics::UserType expected_type;
  bool dev_mode;
  bool guest;
  bool owner;
};

class UserTypeTest : public ::testing::TestWithParam<UserTypeTestParams> {
 public:
  UserTypeTest() {}
  virtual ~UserTypeTest() {}

  int LoginUserTypeCode(bool dev_mode, bool guest, bool owner) {
    return LoginMetrics::LoginUserTypeCode(dev_mode, guest, owner);
  }
};

TEST_P(UserTypeTest, CalculateUserType) {
  EXPECT_TRUE(GetParam().expected_type == LoginUserTypeCode(GetParam().dev_mode,
                                                            GetParam().guest,
                                                            GetParam().owner));
}

INSTANTIATE_TEST_SUITE_P(DevGuest,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::DEV_GUEST, true, true, false)));

INSTANTIATE_TEST_SUITE_P(DevOwner,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::DEV_OWNER, true, false, true)));

INSTANTIATE_TEST_SUITE_P(DevOther,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::DEV_OTHER, true, false, false)));

INSTANTIATE_TEST_SUITE_P(Guest,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::GUEST, false, true, false)));

INSTANTIATE_TEST_SUITE_P(Owner,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::OWNER, false, false, true)));

INSTANTIATE_TEST_SUITE_P(Other,
                         UserTypeTest,
                         ::testing::Values(UserTypeTestParams(
                             LoginMetrics::OTHER, false, false, false)));

}  // namespace login_manager
