//
// Copyright (C) 2011 The Android Open Source Project
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

#include "update_engine/cros/omaha_request_params.h"

#include <stdio.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/fake_system_state.h"

using chromeos_update_engine::test_utils::WriteFileString;
using std::string;

namespace chromeos_update_engine {

class OmahaRequestParamsTest : public ::testing::Test {
 public:
  OmahaRequestParamsTest() : params_() {}

 protected:
  void SetUp() override {
    // Create a uniquely named test directory.
    ASSERT_TRUE(tempdir_.CreateUniqueTempDir());
    params_.set_root(tempdir_.GetPath().value());
    FakeSystemState::CreateInstance();
    SetLockDown(false);
  }

  void SetLockDown(bool locked_down) {
    FakeSystemState::Get()->fake_hardware()->SetIsOfficialBuild(locked_down);
    FakeSystemState::Get()->fake_hardware()->SetIsNormalBootMode(locked_down);
  }

  OmahaRequestParams params_;
  base::ScopedTempDir tempdir_;
};

namespace {
string GetMachineType() {
  string machine_type;
  if (!utils::ReadPipe("uname -m", &machine_type))
    return "";
  // Strip anything from the first newline char.
  size_t newline_pos = machine_type.find('\n');
  if (newline_pos != string::npos)
    machine_type.erase(newline_pos);
  return machine_type;
}
}  // namespace

TEST_F(OmahaRequestParamsTest, MissingChannelTest) {
  EXPECT_TRUE(params_.Init("", "", {}));
  // By default, if no channel is set, we should track the stable-channel.
  EXPECT_EQ(kStableChannel, params_.target_channel());
}

TEST_F(OmahaRequestParamsTest, ForceVersionTest) {
  EXPECT_TRUE(params_.Init("ForcedVersion", "", {}));
  EXPECT_EQ(string("ForcedVersion_") + GetMachineType(), params_.os_sp());
  EXPECT_EQ("ForcedVersion", params_.app_version());
}

TEST_F(OmahaRequestParamsTest, ForcedURLTest) {
  EXPECT_TRUE(params_.Init("", "http://forced.google.com", {}));
  EXPECT_EQ("http://forced.google.com", params_.update_url());
}

TEST_F(OmahaRequestParamsTest, MissingURLTest) {
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(constants::kOmahaDefaultProductionURL, params_.update_url());
}

TEST_F(OmahaRequestParamsTest, DeltaOKTest) {
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_TRUE(params_.delta_okay());
}

TEST_F(OmahaRequestParamsTest, NoDeltasTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append(".nodelta").value(), ""));
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_FALSE(params_.delta_okay());
}

TEST_F(OmahaRequestParamsTest, SetTargetChannelTest) {
  {
    OmahaRequestParams params;
    params.set_root(tempdir_.GetPath().value());
    EXPECT_TRUE(params.Init("", "", {}));
    EXPECT_TRUE(params.SetTargetChannel(kCanaryChannel, false, nullptr));
    EXPECT_FALSE(params.mutable_image_props_.is_powerwash_allowed);
  }
  params_.set_root(tempdir_.GetPath().value());
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(kCanaryChannel, params_.target_channel());
  EXPECT_FALSE(params_.mutable_image_props_.is_powerwash_allowed);
}

TEST_F(OmahaRequestParamsTest, SetTargetCommercialChannelTest) {
  {
    OmahaRequestParams params;
    params.set_root(tempdir_.GetPath().value());
    EXPECT_TRUE(params.Init("", "", {}));
    EXPECT_TRUE(params.SetTargetChannel(kLtcChannel, false, nullptr));
  }
  params_.set_root(tempdir_.GetPath().value());
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(kStableChannel, params_.target_channel());
}

TEST_F(OmahaRequestParamsTest, SetCommercialChannelUsingParamTest) {
  {
    OmahaRequestParams params;
    params.set_root(tempdir_.GetPath().value());
    EXPECT_TRUE(params.Init("", "", {}));
    EXPECT_TRUE(params.SetTargetChannel(kStableChannel, false, nullptr));
  }
  params_.set_root(tempdir_.GetPath().value());
  EXPECT_TRUE(params_.Init("", "", {.target_channel = kLtcChannel}));
  EXPECT_EQ(kLtcChannel, params_.target_channel());
}

TEST_F(OmahaRequestParamsTest, SetIsPowerwashAllowedTest) {
  {
    OmahaRequestParams params;
    params.set_root(tempdir_.GetPath().value());
    EXPECT_TRUE(params.Init("", "", {}));
    EXPECT_TRUE(params.SetTargetChannel(kCanaryChannel, true, nullptr));
    EXPECT_TRUE(params.mutable_image_props_.is_powerwash_allowed);
  }
  params_.set_root(tempdir_.GetPath().value());
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(kCanaryChannel, params_.target_channel());
  EXPECT_TRUE(params_.mutable_image_props_.is_powerwash_allowed);
}

TEST_F(OmahaRequestParamsTest, SetTargetChannelInvalidTest) {
  {
    OmahaRequestParams params;
    params.set_root(tempdir_.GetPath().value());
    SetLockDown(true);
    EXPECT_TRUE(params.Init("", "", {}));
    params.image_props_.allow_arbitrary_channels = false;
    string error_message;
    EXPECT_FALSE(
        params.SetTargetChannel("dogfood-channel", true, &error_message));
    // The error message should include a message about the valid channels.
    EXPECT_NE(string::npos, error_message.find(kStableChannel));
    EXPECT_FALSE(params.mutable_image_props_.is_powerwash_allowed);
  }
  params_.set_root(tempdir_.GetPath().value());
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(kStableChannel, params_.target_channel());
  EXPECT_FALSE(params_.mutable_image_props_.is_powerwash_allowed);
}

TEST_F(OmahaRequestParamsTest, IsValidChannelTest) {
  EXPECT_TRUE(params_.IsValidChannel(kCanaryChannel));
  EXPECT_TRUE(params_.IsValidChannel(kStableChannel));
  EXPECT_TRUE(params_.IsValidChannel(kBetaChannel));
  EXPECT_TRUE(params_.IsValidChannel(kDevChannel));
  EXPECT_TRUE(params_.IsValidChannel(kLtcChannel));
  EXPECT_TRUE(params_.IsValidChannel(kLtsChannel));
  EXPECT_FALSE(params_.IsValidChannel("testimage-channel"));
  EXPECT_FALSE(params_.IsValidChannel("dogfood-channel"));
  EXPECT_FALSE(params_.IsValidChannel("some-channel"));
  EXPECT_FALSE(params_.IsValidChannel("lts-invalid"));
  EXPECT_FALSE(params_.IsValidChannel(""));
  params_.image_props_.allow_arbitrary_channels = true;
  EXPECT_TRUE(params_.IsValidChannel("some-channel"));
  EXPECT_FALSE(params_.IsValidChannel("wrong-suffix"));
  EXPECT_FALSE(params_.IsValidChannel(""));
}

TEST_F(OmahaRequestParamsTest, SetTargetChannelWorks) {
  params_.set_target_channel(kDevChannel);
  EXPECT_EQ(kDevChannel, params_.target_channel());

  // When an invalid value is set, it should be ignored.
  EXPECT_FALSE(params_.SetTargetChannel("invalid-channel", false, nullptr));
  EXPECT_EQ(kDevChannel, params_.target_channel());

  // When set to a valid value, it should take effect.
  EXPECT_TRUE(params_.SetTargetChannel(kBetaChannel, true, nullptr));
  EXPECT_EQ(kBetaChannel, params_.target_channel());

  // When set to the same value, it should be idempotent.
  EXPECT_TRUE(params_.SetTargetChannel(kBetaChannel, true, nullptr));
  EXPECT_EQ(kBetaChannel, params_.target_channel());

  // When set to a valid value while a change is already pending, it should
  // succeed.
  EXPECT_TRUE(params_.SetTargetChannel(kStableChannel, true, nullptr));
  EXPECT_EQ(kStableChannel, params_.target_channel());

  // Set a different channel in mutable_image_props_.
  params_.set_target_channel(kStableChannel);

  // When set to a valid value while a change is already pending, it should
  // succeed.
  params_.Init("", "", {});
  EXPECT_TRUE(params_.SetTargetChannel(kBetaChannel, true, nullptr));
  // The target channel should reflect the change, but the download channel
  // should continue to retain the old value ...
  EXPECT_EQ(kBetaChannel, params_.target_channel());
  EXPECT_EQ(kStableChannel, params_.download_channel());

  // ... until we update the download channel explicitly.
  params_.UpdateDownloadChannel();
  EXPECT_EQ(kBetaChannel, params_.download_channel());
  EXPECT_EQ(kBetaChannel, params_.target_channel());
}

TEST_F(OmahaRequestParamsTest, ChannelIndexTest) {
  int canary = params_.GetChannelIndex(kCanaryChannel);
  int dev = params_.GetChannelIndex(kDevChannel);
  int beta = params_.GetChannelIndex(kBetaChannel);
  int stable = params_.GetChannelIndex(kStableChannel);
  int ltc = params_.GetChannelIndex(kLtcChannel);
  int lts = params_.GetChannelIndex(kLtsChannel);
  EXPECT_LE(canary, dev);
  EXPECT_LE(dev, beta);
  EXPECT_LE(beta, stable);
  EXPECT_LE(stable, ltc);
  EXPECT_LE(ltc, lts);

  // testimage-channel or other names are not recognized, so index will be -1.
  int testimage = params_.GetChannelIndex("testimage-channel");
  int bogus = params_.GetChannelIndex("bogus-channel");
  EXPECT_EQ(-1, testimage);
  EXPECT_EQ(-1, bogus);
}

TEST_F(OmahaRequestParamsTest, ToMoreStableChannelFlagTest) {
  params_.image_props_.current_channel = kCanaryChannel;
  params_.download_channel_ = kStableChannel;
  EXPECT_TRUE(params_.ToMoreStableChannel());
  params_.image_props_.current_channel = kStableChannel;
  EXPECT_FALSE(params_.ToMoreStableChannel());
  params_.download_channel_ = kBetaChannel;
  EXPECT_FALSE(params_.ToMoreStableChannel());
}

TEST_F(OmahaRequestParamsTest, ShouldPowerwashTest) {
  params_.mutable_image_props_.is_powerwash_allowed = false;
  EXPECT_FALSE(params_.ShouldPowerwash());
  params_.mutable_image_props_.is_powerwash_allowed = true;
  params_.image_props_.allow_arbitrary_channels = true;
  params_.image_props_.current_channel = "foo-channel";
  params_.download_channel_ = "bar-channel";
  EXPECT_TRUE(params_.ShouldPowerwash());
  params_.image_props_.allow_arbitrary_channels = false;
  params_.image_props_.current_channel = kCanaryChannel;
  params_.download_channel_ = kStableChannel;
  EXPECT_TRUE(params_.ShouldPowerwash());
}

TEST_F(OmahaRequestParamsTest, RequisitionIsSetTest) {
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ("fake_requisition", params_.device_requisition());
}

TEST_F(OmahaRequestParamsTest, GetMissingDlcId) {
  EXPECT_TRUE(params_.Init("", "", {}));

  string dlc_id;
  EXPECT_FALSE(params_.GetDlcId("some-dlc-app-id", &dlc_id));
}

TEST_F(OmahaRequestParamsTest, GetDlcId) {
  EXPECT_TRUE(params_.Init("", "", {}));
  const string kExpectedDlcId = "test-dlc";
  const string dlc_app_id = params_.GetDlcAppId(kExpectedDlcId);
  params_.set_dlc_apps_params({{dlc_app_id, {.name = kExpectedDlcId}}});

  string dlc_id;
  EXPECT_TRUE(params_.GetDlcId(dlc_app_id, &dlc_id));
  EXPECT_EQ(kExpectedDlcId, dlc_id);
}

TEST_F(OmahaRequestParamsTest, GetDlcAppId) {
  EXPECT_TRUE(params_.Init("", "", {}));
  const string kAppId = "test-app-id";
  params_.set_app_id(kAppId);
  const string kDlcId = "test-dlc";
  const string expected_dlc_app_id = kAppId + "_" + kDlcId;

  EXPECT_EQ(expected_dlc_app_id, params_.GetDlcAppId(kDlcId));
}

TEST_F(OmahaRequestParamsTest, IsMiniOSAppId) {
  EXPECT_TRUE(params_.IsMiniOSAppId("test_minios"));
  // Does not end with valid suffix.
  EXPECT_FALSE(params_.IsMiniOSAppId("test_minios_"));
  EXPECT_FALSE(params_.IsMiniOSAppId("testminios"));

  // Case sensitive.
  EXPECT_FALSE(params_.IsMiniOSAppId("test_MINIOS"));
}

TEST_F(OmahaRequestParamsTest, MiniOsParams) {
  FakeSystemState::Get()->fake_hardware()->SetIsRunningFromMiniOs(true);
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_FALSE(params_.delta_okay());
  EXPECT_EQ(kNoVersion, params_.app_version());
}

TEST_F(OmahaRequestParamsTest, IsCommercialChannel) {
  EXPECT_TRUE(OmahaRequestParams::IsCommercialChannel(kLtsChannel));
  EXPECT_TRUE(OmahaRequestParams::IsCommercialChannel(kLtcChannel));
  EXPECT_FALSE(OmahaRequestParams::IsCommercialChannel(kStableChannel));
  EXPECT_FALSE(OmahaRequestParams::IsCommercialChannel("foo-channel"));
}

TEST_F(OmahaRequestParamsTest, NoFSIVersionResultsInEmptyString) {
  FakeSystemState::Get()->fake_hardware()->SetFsiVersion("");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "");
  EXPECT_EQ(params_.activate_date(), "");
}

TEST_F(OmahaRequestParamsTest, NoActivateDateResultsInEmptyString) {
  FakeSystemState::Get()->fake_hardware()->SetActivateDate("");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "");
  EXPECT_EQ(params_.activate_date(), "");
}

TEST_F(OmahaRequestParamsTest, FSIVersionComesFromHardware) {
  FakeSystemState::Get()->fake_hardware()->SetFsiVersion("12345.1.1");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "12345.1.1");
  EXPECT_EQ(params_.activate_date(), "");
}

TEST_F(OmahaRequestParamsTest, ActivateDateComesFromHardware) {
  FakeSystemState::Get()->fake_hardware()->SetActivateDate("2022-00");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "");
  EXPECT_EQ(params_.activate_date(), "2022-00");
}

TEST_F(OmahaRequestParamsTest, MalformatedFSIVersionIsIgnored) {
  FakeSystemState::Get()->fake_hardware()->SetFsiVersion("1234.PATCH");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "");
  EXPECT_EQ(params_.activate_date(), "");
}

TEST_F(OmahaRequestParamsTest, ImpossibleActivateDateIsIgnored) {
  // Weeks go from 00 to 53.
  FakeSystemState::Get()->fake_hardware()->SetActivateDate("2022-54");
  EXPECT_TRUE(params_.Init("", "", {}));
  EXPECT_EQ(params_.fsi_version(), "");
  EXPECT_EQ(params_.activate_date(), "");
}

}  // namespace chromeos_update_engine
