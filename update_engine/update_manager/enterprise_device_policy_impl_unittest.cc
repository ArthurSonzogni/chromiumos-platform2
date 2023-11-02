//
// Copyright (C) 2020 The Android Open Source Project
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

#include "update_engine/update_manager/enterprise_device_policy_impl.h"

#include <memory>
#include <utility>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"

using chromeos_update_engine::FakeSystemState;
using chromeos_update_engine::kStableChannel;
using std::string;

namespace chromeos_update_manager {

class UmEnterpriseDevicePolicyImplTest : public UmPolicyTestBase {
 protected:
  UmEnterpriseDevicePolicyImplTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCheckAllowedPolicyData());
    policy_2_.reset(new EnterpriseDevicePolicyImpl);

    uca_data_ = static_cast<typeof(uca_data_)>(policy_data_.get());
  }

  void SetUp() override {
    UmPolicyTestBase::SetUp();
    fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
        new bool(true));

    FakeSystemState::CreateInstance();
    // Set to mock |UpdateAttempter|.
    FakeSystemState::Get()->set_update_attempter(nullptr);
    ON_CALL(*FakeSystemState::Get()->mock_update_attempter(), IsUpdating())
        .WillByDefault(testing::Return(true));
  }

  // Sets the policies required for a kiosk app to control Chrome OS version:
  // - AllowKioskAppControlChromeVersion = True
  // - UpdateDisabled = True
  // In the kiosk app manifest:
  // - RequiredPlatformVersion = 1234.
  void SetKioskAppControlsChromeOsVersion() {
    fake_state_.device_policy_provider()
        ->var_allow_kiosk_app_control_chrome_version()
        ->reset(new bool(true));
    fake_state_.device_policy_provider()->var_update_disabled()->reset(
        new bool(true));
    fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
        new string("1234."));
  }

  UpdateCheckAllowedPolicyData* uca_data_;
};

TEST_F(UmEnterpriseDevicePolicyImplTest, KioskAppVersionSet) {
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));

  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      new std::string("1234.5.6"));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(uca_data_->update_check_params.target_version_prefix, "1234.5.6");
}

TEST_F(UmEnterpriseDevicePolicyImplTest, KioskAppVersionUnreadableNoUpdate) {
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));

  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      nullptr);

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseDevicePolicyImplTest, KioskAppVersionUnreadableUpdate) {
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));

  // The real variable returns an empty string after several unsuccessful
  // reading attempts. Fake this by setting it directly to empty string.
  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      new std::string(""));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(uca_data_->update_check_params.target_version_prefix, "");
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       KioskAppVersionUnreadableUpdateWithMinVersion) {
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));

  // The real variable returns an empty string after several unsuccessful
  // reading attempts. Fake this by setting it directly to empty string.
  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      new std::string(""));
  // Update if the minimum version is above the current OS version.
  fake_state_.device_policy_provider()->var_device_minimum_version()->reset(
      new base::Version("2.0.0"));
  fake_state_.system_provider()->var_chromeos_version()->reset(
      new base::Version("1.0.0"));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(uca_data_->update_check_params.target_version_prefix, "");
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       KioskAppVersionUnreadableNoUpdateWithMinVersion) {
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));

  // The real variable returns an empty string after several unsuccessful
  // reading attempts. Fake this by setting it directly to empty string.
  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      new std::string(""));
  // Block update if the minimum version is below the current OS version.
  fake_state_.device_policy_provider()->var_device_minimum_version()->reset(
      new base::Version("1.0.0"));
  fake_state_.system_provider()->var_chromeos_version()->reset(
      new base::Version("2.0.0"));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseDevicePolicyImplTest, ChannelDowngradeBehaviorNoRollback) {
  fake_state_.device_policy_provider()->var_release_channel_delegated()->reset(
      new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->reset(
      new std::string(kStableChannel));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_FALSE(uca_data_->update_check_params.rollback_on_channel_downgrade);
}

TEST_F(UmEnterpriseDevicePolicyImplTest, ChannelDowngradeBehaviorRollback) {
  fake_state_.device_policy_provider()->var_release_channel_delegated()->reset(
      new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->reset(
      new std::string(kStableChannel));
  fake_state_.device_policy_provider()->var_channel_downgrade_behavior()->reset(
      new ChannelDowngradeBehavior(ChannelDowngradeBehavior::kRollback));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.rollback_on_channel_downgrade);
}

TEST_F(UmEnterpriseDevicePolicyImplTest, UpdateCheckAllowedKioskPin) {
  SetKioskAppControlsChromeOsVersion();

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_EQ("1234.", uca_data_->update_check_params.target_version_prefix);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       UpdateCheckAllowedDisabledWhenNoKioskPin) {
  // Disable AU policy is set but kiosk pin policy is set to false. Update is
  // disabled in such case.
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(false));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       UpdateCheckAllowedKioskPinWithNoRequiredVersion) {
  // AU disabled, allow kiosk to pin but there is no kiosk required platform
  // version (i.e. app does not provide the info). Update to latest in such
  // case.
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));
  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      new string());

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_TRUE(uca_data_->update_check_params.target_version_prefix.empty());
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       UpdateCheckAllowedKioskPinWithFailedGetRequiredVersionCall) {
  // AU disabled, allow kiosk to pin but D-Bus call to get required platform
  // version failed. Defer update check in this case.
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()
      ->var_allow_kiosk_app_control_chrome_version()
      ->reset(new bool(true));
  fake_state_.system_provider()->var_kiosk_required_platform_version()->reset(
      nullptr);

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseDevicePolicyImplTest,
       UpdateCheckAllowedInstallationsNotBlocked) {
  fake_state_.system_provider()->var_is_updating()->reset(new bool(false));
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
