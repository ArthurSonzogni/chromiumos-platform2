//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/common_service.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/cros/omaha_utils.h"

using std::string;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using update_engine::UpdateAttemptFlags;
using update_engine::UpdateParams;

namespace chromeos_update_engine {

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class UpdateEngineServiceTest : public ::testing::Test {
 protected:
  UpdateEngineServiceTest() = default;

  void SetUp() override {
    FakeSystemState::CreateInstance();
    FakeSystemState::Get()->set_device_policy(nullptr);
    mock_update_attempter_ = FakeSystemState::Get()->mock_update_attempter();
  }

  MockUpdateAttempter* mock_update_attempter_;

  brillo::ErrorPtr error_;
  UpdateEngineService common_service_;
};

TEST_F(UpdateEngineServiceTest, Update) {
  UpdateParams update_params;
  update_params.set_app_version("app_ver");
  update_params.set_omaha_url("url");
  update_params.mutable_update_flags()->set_non_interactive(true);
  EXPECT_CALL(*mock_update_attempter_,
              CheckForUpdate(EqualsProto(update_params)))
      .WillOnce(Return(true));

  // The non-interactive flag needs to be passed through to CheckForUpdate.
  bool result = false;
  EXPECT_TRUE(common_service_.Update(&error_, update_params, &result));
  EXPECT_EQ(nullptr, error_);
  EXPECT_TRUE(result);
}

TEST_F(UpdateEngineServiceTest, UpdateReturnsFalse) {
  UpdateParams update_params;
  update_params.set_app_version("app_ver");
  update_params.set_omaha_url("url");
  update_params.mutable_update_flags()->set_non_interactive(false);
  EXPECT_CALL(*mock_update_attempter_,
              CheckForUpdate(EqualsProto(update_params)))
      .WillOnce(Return(false));
  bool result = true;
  EXPECT_TRUE(common_service_.Update(&error_, update_params, &result));
  EXPECT_EQ(nullptr, error_);
  EXPECT_FALSE(result);
}

TEST_F(UpdateEngineServiceTest, AttemptInstall) {
  EXPECT_CALL(*mock_update_attempter_, CheckForInstall(_, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(common_service_.AttemptInstall(&error_, "", {}));
  EXPECT_EQ(nullptr, error_);
}

TEST_F(UpdateEngineServiceTest, AttemptInstallReturnsFalse) {
  EXPECT_CALL(*mock_update_attempter_, CheckForInstall(_, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(common_service_.AttemptInstall(&error_, "", {}));
}

TEST_F(UpdateEngineServiceTest, SetDlcActiveValue) {
  EXPECT_CALL(*mock_update_attempter_, SetDlcActiveValue(_, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(common_service_.SetDlcActiveValue(&error_, true, "dlc0"));
}

TEST_F(UpdateEngineServiceTest, SetDlcActiveValueReturnsFalse) {
  EXPECT_CALL(*mock_update_attempter_, SetDlcActiveValue(_, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(common_service_.SetDlcActiveValue(&error_, true, "dlc0"));
}

// SetChannel is allowed when there's no device policy (the device is not
// enterprise enrolled).
TEST_F(UpdateEngineServiceTest, SetChannelWithNoPolicy) {
  EXPECT_CALL(*mock_update_attempter_, RefreshDevicePolicy());
  // If SetTargetChannel is called it means the policy check passed.
  EXPECT_CALL(*FakeSystemState::Get()->mock_request_params(),
              SetTargetChannel(kStableChannel, true, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(common_service_.SetChannel(&error_, kStableChannel, true));
  ASSERT_EQ(nullptr, error_);
}

// When the policy is present, the delegated value should be checked.
TEST_F(UpdateEngineServiceTest, SetChannelWithDelegatedPolicy) {
  policy::MockDevicePolicy mock_device_policy;
  FakeSystemState::Get()->set_device_policy(&mock_device_policy);
  EXPECT_CALL(mock_device_policy, GetReleaseChannelDelegated(_))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(true)));
  EXPECT_CALL(*FakeSystemState::Get()->mock_request_params(),
              SetTargetChannel(kBetaChannel, true, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(common_service_.SetChannel(&error_, kBetaChannel, true));
  ASSERT_EQ(nullptr, error_);
}

// When passing an invalid value (SetTargetChannel fails) an error should be
// raised.
TEST_F(UpdateEngineServiceTest, SetChannelWithInvalidChannel) {
  EXPECT_CALL(*mock_update_attempter_, RefreshDevicePolicy());
  EXPECT_CALL(*FakeSystemState::Get()->mock_request_params(),
              SetTargetChannel("foo-channel", true, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(common_service_.SetChannel(&error_, "foo-channel", true));
  ASSERT_NE(nullptr, error_);
  EXPECT_TRUE(error_->HasError(UpdateEngineService::kErrorDomain,
                               UpdateEngineService::kErrorFailed));
}

// When passing lts channel, an error should be raised.
TEST_F(UpdateEngineServiceTest, SetChannelWithCommercialChannelLts) {
  EXPECT_CALL(*mock_update_attempter_, RefreshDevicePolicy());

  EXPECT_FALSE(common_service_.SetChannel(&error_, kLtsChannel, true));
  ASSERT_NE(nullptr, error_);
  EXPECT_TRUE(error_->HasError(UpdateEngineService::kErrorDomain,
                               UpdateEngineService::kErrorFailed));
}

// When passing ltc only channel, an error should be raised.
TEST_F(UpdateEngineServiceTest, SetChannelWithCommercialChannelLtc) {
  EXPECT_CALL(*mock_update_attempter_, RefreshDevicePolicy());

  EXPECT_FALSE(common_service_.SetChannel(&error_, kLtcChannel, true));
  ASSERT_NE(nullptr, error_);
  EXPECT_TRUE(error_->HasError(UpdateEngineService::kErrorDomain,
                               UpdateEngineService::kErrorFailed));
}

TEST_F(UpdateEngineServiceTest, GetChannel) {
  FakeSystemState::Get()->mock_request_params()->set_current_channel("current");
  FakeSystemState::Get()->mock_request_params()->set_target_channel("target");
  string channel;
  EXPECT_TRUE(common_service_.GetChannel(
      &error_, true /* get_current_channel */, &channel));
  EXPECT_EQ(nullptr, error_);
  EXPECT_EQ("current", channel);

  EXPECT_TRUE(common_service_.GetChannel(
      &error_, false /* get_current_channel */, &channel));
  EXPECT_EQ(nullptr, error_);
  EXPECT_EQ("target", channel);
}

TEST_F(UpdateEngineServiceTest, ResetStatusSucceeds) {
  EXPECT_CALL(*mock_update_attempter_, ResetStatus()).WillOnce(Return(true));
  EXPECT_TRUE(common_service_.ResetStatus(&error_));
  EXPECT_EQ(nullptr, error_);
}

TEST_F(UpdateEngineServiceTest, ResetStatusFails) {
  EXPECT_CALL(*mock_update_attempter_, ResetStatus()).WillOnce(Return(false));
  EXPECT_FALSE(common_service_.ResetStatus(&error_));
  ASSERT_NE(nullptr, error_);
  EXPECT_TRUE(error_->HasError(UpdateEngineService::kErrorDomain,
                               UpdateEngineService::kErrorFailed));
}

}  // namespace chromeos_update_engine
