// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "policy/device_policy_impl.h"

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "install_attributes/libinstallattributes.h"
#include "install_attributes/mock_install_attributes_reader.h"
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>

namespace em = enterprise_management;

using testing::ElementsAre;

namespace policy {

class DevicePolicyImplTest : public testing::Test, public DevicePolicyImpl {
 protected:
  void InitializePolicyForConsumer() { InitializePolicy(); }

  void InitializePolicyForEnterprise() {
    InitializePolicy(InstallAttributesReader::kDeviceModeEnterprise);
  }

  em::ChromeDeviceSettingsProto device_policy_proto_;
  em::PolicyData policy_data_;

  DevicePolicyImpl device_policy_;

 private:
  // When |device_mode| is empty, the function assumes it's a customer
  // owned device and sets empty install attributes.
  void InitializePolicy(const std::string& device_mode = "") {
    device_policy_.set_policy_for_testing(device_policy_proto_);
    device_policy_.set_policy_data_for_testing(policy_data_);

    if (device_mode.empty()) {
      device_policy_.set_install_attributes_for_testing(
          std::make_unique<MockInstallAttributesReader>(
              cryptohome::SerializedInstallAttributes()));
    } else {
      device_policy_.set_install_attributes_for_testing(
          std::make_unique<MockInstallAttributesReader>(
              device_mode, true /* initialized */));
    }
  }
};

// Enterprise managed.
TEST_F(DevicePolicyImplTest, GetOwner_Managed) {
  policy_data_.set_username("user@example.com");
  policy_data_.set_management_mode(em::PolicyData::ENTERPRISE_MANAGED);
  InitializePolicyForEnterprise();

  std::string owner("something");
  EXPECT_TRUE(device_policy_.GetOwner(&owner));
  EXPECT_TRUE(owner.empty());
}

// Consumer owned.
TEST_F(DevicePolicyImplTest, GetOwner_Consumer) {
  policy_data_.set_username("user@example.com");
  policy_data_.set_management_mode(em::PolicyData::LOCAL_OWNER);
  policy_data_.set_request_token("codepath-must-ignore-dmtoken");
  InitializePolicyForConsumer();

  std::string owner;
  EXPECT_TRUE(device_policy_.GetOwner(&owner));
  EXPECT_EQ("user@example.com", owner);
}

// Consumer owned, username is missing.
TEST_F(DevicePolicyImplTest, GetOwner_ConsumerMissingUsername) {
  InitializePolicyForConsumer();

  std::string owner("something");
  EXPECT_FALSE(device_policy_.GetOwner(&owner));
  EXPECT_EQ("something", owner);
}

// RollbackAllowedMilestones is not set.
TEST_F(DevicePolicyImplTest, GetRollbackAllowedMilestones_NotSet) {
  InitializePolicyForEnterprise();

  int value = -1;
  ASSERT_TRUE(device_policy_.GetRollbackAllowedMilestones(&value));
  EXPECT_EQ(4, value);
}

// RollbackAllowedMilestones is set to a valid value.
TEST_F(DevicePolicyImplTest, GetRollbackAllowedMilestones_Set) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_rollback_allowed_milestones(3);
  InitializePolicyForEnterprise();

  int value = -1;
  ASSERT_TRUE(device_policy_.GetRollbackAllowedMilestones(&value));
  EXPECT_EQ(3, value);
}

// RollbackAllowedMilestones is set to a valid value, but it's not an enterprise
// device.
TEST_F(DevicePolicyImplTest, GetRollbackAllowedMilestones_SetConsumer) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_rollback_allowed_milestones(3);
  InitializePolicyForConsumer();

  int value = -1;
  ASSERT_FALSE(device_policy_.GetRollbackAllowedMilestones(&value));
}

// RollbackAllowedMilestones is set to an invalid value.
TEST_F(DevicePolicyImplTest, GetRollbackAllowedMilestones_SetTooLarge) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_rollback_allowed_milestones(10);
  InitializePolicyForEnterprise();

  int value = -1;
  ASSERT_TRUE(device_policy_.GetRollbackAllowedMilestones(&value));
  EXPECT_EQ(4, value);
}

// RollbackAllowedMilestones is set to an invalid value.
TEST_F(DevicePolicyImplTest, GetRollbackAllowedMilestones_SetTooSmall) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_rollback_allowed_milestones(-1);
  InitializePolicyForEnterprise();

  int value = -1;
  ASSERT_TRUE(device_policy_.GetRollbackAllowedMilestones(&value));
  EXPECT_EQ(0, value);
}

// Update staging schedule has no values
TEST_F(DevicePolicyImplTest, GetDeviceUpdateStagingSchedule_NoValues) {
  device_policy_proto_.mutable_auto_update_settings()->set_staging_schedule(
      "[]");
  InitializePolicyForEnterprise();

  std::vector<DayPercentagePair> staging_schedule;
  ASSERT_TRUE(device_policy_.GetDeviceUpdateStagingSchedule(&staging_schedule));
  EXPECT_EQ(0, staging_schedule.size());
}

// Update staging schedule has valid values
TEST_F(DevicePolicyImplTest, GetDeviceUpdateStagingSchedule_Valid) {
  device_policy_proto_.mutable_auto_update_settings()->set_staging_schedule(
      "[{\"days\": 4, \"percentage\": 40}, {\"days\": 10, \"percentage\": "
      "100}]");
  InitializePolicyForEnterprise();

  std::vector<DayPercentagePair> staging_schedule;
  ASSERT_TRUE(device_policy_.GetDeviceUpdateStagingSchedule(&staging_schedule));
  EXPECT_THAT(staging_schedule, ElementsAre(DayPercentagePair{4, 40},
                                            DayPercentagePair{10, 100}));
}

// Update staging schedule has values with values set larger than the max
// allowed days/percentage and smaller than the min allowed days/percentage.
TEST_F(DevicePolicyImplTest,
       GetDeviceUpdateStagingSchedule_SetOutsideAllowable) {
  device_policy_proto_.mutable_auto_update_settings()->set_staging_schedule(
      "[{\"days\": -1, \"percentage\": -10}, {\"days\": 30, \"percentage\": "
      "110}]");
  InitializePolicyForEnterprise();

  std::vector<DayPercentagePair> staging_schedule;
  ASSERT_TRUE(device_policy_.GetDeviceUpdateStagingSchedule(&staging_schedule));
  EXPECT_THAT(staging_schedule,
              ElementsAre(DayPercentagePair{1, 0}, DayPercentagePair{28, 100}));
}

// Updates should only be disabled for enterprise managed devices.
TEST_F(DevicePolicyImplTest, GetUpdateDisabled_SetConsumer) {
  device_policy_proto_.mutable_auto_update_settings()->set_update_disabled(
      true);
  InitializePolicyForConsumer();

  bool value;
  ASSERT_FALSE(device_policy_.GetUpdateDisabled(&value));
}

// Updates should only be pinned on enterprise managed devices.
TEST_F(DevicePolicyImplTest, GetTargetVersionPrefix_SetConsumer) {
  em::AutoUpdateSettingsProto* auto_update_settings =
      device_policy_proto_.mutable_auto_update_settings();
  auto_update_settings->set_target_version_prefix("hello");
  InitializePolicyForConsumer();

  std::string value = "";
  ASSERT_FALSE(device_policy_.GetTargetVersionPrefix(&value));
}

// The allowed connection types should only be changed in enterprise devices.
TEST_F(DevicePolicyImplTest, GetAllowedConnectionTypesForUpdate_SetConsumer) {
  device_policy_proto_.mutable_auto_update_settings()
      ->add_allowed_connection_types(
          em::AutoUpdateSettingsProto::CONNECTION_TYPE_ETHERNET);
  InitializePolicyForConsumer();

  std::set<std::string> value;
  ASSERT_FALSE(device_policy_.GetAllowedConnectionTypesForUpdate(&value));
}

// Update time restrictions should only be used in enterprise devices.
TEST_F(DevicePolicyImplTest, GetDisallowedTimeIntervals_SetConsumer) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_disallowed_time_intervals(
          "[{\"start\": {\"day_of_week\": \"Monday\", \"hours\": 10, "
          "\"minutes\": "
          "0}, \"end\": {\"day_of_week\": \"Monday\", \"hours\": 10, "
          "\"minutes\": "
          "0}}]");
  InitializePolicyForConsumer();

  std::vector<WeeklyTimeInterval> value;
  ASSERT_FALSE(device_policy_.GetDisallowedTimeIntervals(&value));
}

// |DeviceQuickFixBuildToken| is set when device is enterprise enrolled.
TEST_F(DevicePolicyImplTest, GetDeviceQuickFixBuildToken_Set) {
  const char kToken[] = "some_token";

  device_policy_proto_.mutable_auto_update_settings()
      ->set_device_quick_fix_build_token(kToken);
  InitializePolicyForEnterprise();

  std::string value;
  EXPECT_TRUE(device_policy_.GetDeviceQuickFixBuildToken(&value));
  EXPECT_EQ(value, kToken);
}

// If the device is not enterprise-enrolled, |GetDeviceQuickFixBuildToken|
// does not provide a token even if it is present in local device settings.
TEST_F(DevicePolicyImplTest, GetDeviceQuickFixBuildToken_NotSet) {
  const char kToken[] = "some_token";

  device_policy_proto_.mutable_auto_update_settings()
      ->set_device_quick_fix_build_token(kToken);
  InitializePolicyForConsumer();

  std::string value;
  EXPECT_FALSE(device_policy_.GetDeviceQuickFixBuildToken(&value));
  EXPECT_TRUE(value.empty());
}

// Should only write a value and return true if the ID is present.
TEST_F(DevicePolicyImplTest, GetDeviceDirectoryApiId_Set) {
  constexpr char kDummyDeviceId[] = "aa-bb-cc-dd";

  policy_data_.set_directory_api_id(kDummyDeviceId);
  InitializePolicyForConsumer();

  EXPECT_THAT(device_policy_.GetDeviceDirectoryApiId(),
              testing::Optional(std::string(kDummyDeviceId)));
}

TEST_F(DevicePolicyImplTest, GetDeviceDirectoryApiId_NotSet) {
  InitializePolicyForConsumer();

  EXPECT_FALSE(device_policy_.GetDeviceDirectoryApiId().has_value());
}

// Should only write a value and return true as the ID should be present.
TEST_F(DevicePolicyImplTest, GetCustomerId_Set) {
  constexpr char kDummyCustomerId[] = "customerId";

  policy_data_.set_obfuscated_customer_id(kDummyCustomerId);
  InitializePolicyForConsumer();

  std::string id;
  EXPECT_TRUE(device_policy_.GetCustomerId(&id));
  EXPECT_EQ(kDummyCustomerId, id);
}

TEST_F(DevicePolicyImplTest, GetCustomerId_NotSet) {
  InitializePolicyForConsumer();

  std::string id;
  EXPECT_FALSE(device_policy_.GetCustomerId(&id));
  EXPECT_TRUE(id.empty());
}

TEST_F(DevicePolicyImplTest, GetReleaseLtsTagSet) {
  const char kLtsTag[] = "abc";

  device_policy_proto_.mutable_release_channel()->set_release_lts_tag(kLtsTag);
  InitializePolicyForEnterprise();

  std::string lts_tag;
  EXPECT_TRUE(device_policy_.GetReleaseLtsTag(&lts_tag));
  EXPECT_EQ(lts_tag, kLtsTag);
}

TEST_F(DevicePolicyImplTest, GetReleaseLtsTagNotSet) {
  constexpr char kChannel[] = "stable-channel";

  InitializePolicyForEnterprise();

  std::string lts_tag;
  EXPECT_FALSE(device_policy_.GetReleaseLtsTag(&lts_tag));
  EXPECT_TRUE(lts_tag.empty());

  // Add release_channel without lts_tag to the proto by setting an unrelated
  // field.
  device_policy_proto_.mutable_release_channel()->set_release_channel(kChannel);
  InitializePolicyForEnterprise();

  EXPECT_FALSE(device_policy_.GetReleaseLtsTag(&lts_tag));
  EXPECT_TRUE(lts_tag.empty());
}

TEST_F(DevicePolicyImplTest, GetChannelDowngradeBehaviorSet) {
  device_policy_proto_.mutable_auto_update_settings()
      ->set_channel_downgrade_behavior(
          em::AutoUpdateSettingsProto::ChannelDowngradeBehavior ::
              AutoUpdateSettingsProto_ChannelDowngradeBehavior_ROLLBACK);
  InitializePolicyForEnterprise();

  int value = -1;
  EXPECT_TRUE(device_policy_.GetChannelDowngradeBehavior(&value));
  EXPECT_EQ(static_cast<int>(
                em::AutoUpdateSettingsProto::ChannelDowngradeBehavior ::
                    AutoUpdateSettingsProto_ChannelDowngradeBehavior_ROLLBACK),
            value);
}

TEST_F(DevicePolicyImplTest, GetChannelDowngradeBehaviorNotSet) {
  InitializePolicyForConsumer();

  int value = -1;
  EXPECT_FALSE(device_policy_.GetChannelDowngradeBehavior(&value));
}

// Device minimum required version should only be used in enterprise devices.
TEST_F(DevicePolicyImplTest, GetHighestDeviceMinimumVersion_SetConsumer) {
  device_policy_proto_.mutable_device_minimum_version()->set_value(
      "{\"requirements\" : [{\"chromeos_version\" : \"12215\", "
      "\"warning_period\" : 7, \"aue_warning_period\" : 14},  "
      "{\"chromeos_version\" : \"13315.60.12\", \"warning_period\" : 5, "
      "\"aue_warning_period\" : 13}], \"unmanaged_user_restricted\" : true}");
  InitializePolicyForConsumer();

  base::Version version;
  ASSERT_FALSE(device_policy_.GetHighestDeviceMinimumVersion(&version));
}

// Should only write a value and return true as the
// |device_market_segment| should be present.
TEST_F(DevicePolicyImplTest, GetDeviceMarketSegment_EducationDevice) {
  policy_data_.set_market_segment(em::PolicyData::ENROLLED_EDUCATION);
  InitializePolicyForConsumer();

  DeviceMarketSegment segment;
  EXPECT_TRUE(device_policy_.GetDeviceMarketSegment(&segment));
  EXPECT_EQ(segment, DeviceMarketSegment::kEducation);
}

TEST_F(DevicePolicyImplTest, GetDeviceMarketSegment_UnspecifiedDevice) {
  policy_data_.set_market_segment(em::PolicyData::MARKET_SEGMENT_UNSPECIFIED);
  InitializePolicyForConsumer();

  DeviceMarketSegment segment;
  EXPECT_TRUE(device_policy_.GetDeviceMarketSegment(&segment));
  EXPECT_EQ(segment, DeviceMarketSegment::kUnknown);
}

TEST_F(DevicePolicyImplTest, GetDeviceMarketSegment_NotSet) {
  InitializePolicyForConsumer();

  DeviceMarketSegment segment;
  EXPECT_FALSE(device_policy_.GetDeviceMarketSegment(&segment));
}

TEST_F(DevicePolicyImplTest,
       GetDeviceKeylockerForStorageEncryptionEnabled_SetEnabled) {
  device_policy_proto_.mutable_keylocker_for_storage_encryption_enabled()
      ->set_enabled(true);
  InitializePolicyForEnterprise();

  bool kl_enabled = false;
  EXPECT_TRUE(device_policy_.GetDeviceKeylockerForStorageEncryptionEnabled(
      &kl_enabled));
  EXPECT_TRUE(kl_enabled);
}

TEST_F(DevicePolicyImplTest,
       GetDeviceKeylockerForStorageEncryptionEnabled_NotSet) {
  InitializePolicyForConsumer();

  bool kl_enabled = false;
  EXPECT_FALSE(device_policy_.GetDeviceKeylockerForStorageEncryptionEnabled(
      &kl_enabled));
}

// Policy should only apply to enterprise devices.
TEST_F(DevicePolicyImplTest, GetRunAutomaticCleanupOnLogin_SetConsumer) {
  device_policy_proto_.mutable_device_run_automatic_cleanup_on_login()
      ->set_value(true);
  InitializePolicyForConsumer();

  ASSERT_THAT(device_policy_.GetRunAutomaticCleanupOnLogin(),
              testing::Eq(std::nullopt));
}

TEST_F(DevicePolicyImplTest, GetRunAutomaticCleanupOnLogin_Set) {
  device_policy_proto_.mutable_device_run_automatic_cleanup_on_login()
      ->set_value(true);
  InitializePolicyForEnterprise();

  ASSERT_THAT(device_policy_.GetRunAutomaticCleanupOnLogin(),
              testing::Eq(std::optional(true)));
}

TEST_F(DevicePolicyImplTest, GetDeviceReportXDREvents_NotSet) {
  InitializePolicyForEnterprise();

  ASSERT_THAT(device_policy_.GetDeviceReportXDREvents(),
              testing::Eq(std::nullopt));
}

TEST_F(DevicePolicyImplTest, GetDeviceReportXDREvents_Set) {
  device_policy_proto_.mutable_device_report_xdr_events()->set_enabled(true);
  InitializePolicyForEnterprise();

  ASSERT_THAT(device_policy_.GetDeviceReportXDREvents(),
              testing::Eq(std::optional(true)));
}

TEST_F(DevicePolicyImplTest, GetEphemeralSettings_NotSet) {
  InitializePolicyForEnterprise();

  EXPECT_EQ(device_policy_.GetEphemeralSettings(), std::nullopt);
}

TEST_F(DevicePolicyImplTest,
       GetEphemeralSettings_Set_EphemeralUsersEnabled_True) {
  device_policy_proto_.mutable_ephemeral_users_enabled()
      ->set_ephemeral_users_enabled(true);
  InitializePolicyForEnterprise();

  std::optional<DevicePolicy::EphemeralSettings> ephemeral_settings =
      device_policy_.GetEphemeralSettings();
  ASSERT_TRUE(ephemeral_settings.has_value());
  EXPECT_TRUE(ephemeral_settings->global_ephemeral_users_enabled);
  EXPECT_TRUE(ephemeral_settings->specific_ephemeral_users.empty());
  EXPECT_TRUE(ephemeral_settings->specific_nonephemeral_users.empty());
}

TEST_F(DevicePolicyImplTest,
       GetEphemeralSettings_Set_EphemeralUsersEnabled_False) {
  device_policy_proto_.mutable_ephemeral_users_enabled()
      ->set_ephemeral_users_enabled(false);
  InitializePolicyForEnterprise();

  std::optional<DevicePolicy::EphemeralSettings> ephemeral_settings =
      device_policy_.GetEphemeralSettings();
  ASSERT_TRUE(ephemeral_settings.has_value());
  EXPECT_FALSE(ephemeral_settings->global_ephemeral_users_enabled);
  EXPECT_TRUE(ephemeral_settings->specific_ephemeral_users.empty());
  EXPECT_TRUE(ephemeral_settings->specific_nonephemeral_users.empty());
}

TEST_F(DevicePolicyImplTest, GetEphemeralSettings_Set_Non_Ephemeral_User) {
  em::DeviceLocalAccountInfoProto* account =
      device_policy_proto_.mutable_device_local_accounts()->add_account();
  account->set_account_id("account");
  account->set_ephemeral_mode(
      em::DeviceLocalAccountInfoProto::EPHEMERAL_MODE_DISABLE);

  InitializePolicyForEnterprise();

  std::optional<DevicePolicy::EphemeralSettings> ephemeral_settings =
      device_policy_.GetEphemeralSettings();
  ASSERT_TRUE(ephemeral_settings.has_value());
  EXPECT_FALSE(ephemeral_settings->global_ephemeral_users_enabled);
  EXPECT_TRUE(ephemeral_settings->specific_ephemeral_users.empty());
  EXPECT_EQ(1, ephemeral_settings->specific_nonephemeral_users.size());
  EXPECT_EQ("6163636f756e74@public-accounts.device-local.localhost",
            ephemeral_settings->specific_nonephemeral_users[0]);
}

TEST_F(DevicePolicyImplTest, GetEphemeralSettings_Set_Ephemeral_User) {
  em::DeviceLocalAccountInfoProto* account =
      device_policy_proto_.mutable_device_local_accounts()->add_account();
  account->set_account_id("account");
  account->set_ephemeral_mode(
      em::DeviceLocalAccountInfoProto::EPHEMERAL_MODE_ENABLE);

  InitializePolicyForEnterprise();

  std::optional<DevicePolicy::EphemeralSettings> ephemeral_settings =
      device_policy_.GetEphemeralSettings();
  ASSERT_TRUE(ephemeral_settings.has_value());
  EXPECT_FALSE(ephemeral_settings->global_ephemeral_users_enabled);
  EXPECT_EQ(1, ephemeral_settings->specific_ephemeral_users.size());
  EXPECT_EQ("6163636f756e74@public-accounts.device-local.localhost",
            ephemeral_settings->specific_ephemeral_users[0]);
  EXPECT_TRUE(ephemeral_settings->specific_nonephemeral_users.empty());
}

TEST_F(DevicePolicyImplTest, GetEphemeralSettings_Set_EphemeralMode_Unset) {
  device_policy_proto_.mutable_ephemeral_users_enabled()
      ->set_ephemeral_users_enabled(true);
  em::DeviceLocalAccountsProto* device_local_accounts =
      device_policy_proto_.mutable_device_local_accounts();

  em::DeviceLocalAccountInfoProto* account1 =
      device_local_accounts->add_account();
  account1->set_account_id("account1");
  account1->set_ephemeral_mode(
      em::DeviceLocalAccountInfoProto::EPHEMERAL_MODE_UNSET);

  em::DeviceLocalAccountInfoProto* account2 =
      device_local_accounts->add_account();
  account2->set_account_id("account2");
  account2->set_ephemeral_mode(em::DeviceLocalAccountInfoProto::
                                   EPHEMERAL_MODE_FOLLOW_DEVICE_WIDE_POLICY);

  InitializePolicyForEnterprise();

  std::optional<DevicePolicy::EphemeralSettings> ephemeral_settings =
      device_policy_.GetEphemeralSettings();
  ASSERT_TRUE(ephemeral_settings.has_value());
  EXPECT_TRUE(ephemeral_settings->global_ephemeral_users_enabled);
  EXPECT_TRUE(ephemeral_settings->specific_ephemeral_users.empty());
  EXPECT_TRUE(ephemeral_settings->specific_nonephemeral_users.empty());
}

TEST_F(DevicePolicyImplTest, GetDeviceExtendedAutoUpdateEnabled_Set) {
  device_policy_proto_.mutable_deviceextendedautoupdateenabled()->set_value(
      true);

  InitializePolicyForEnterprise();

  EXPECT_TRUE(*device_policy_.GetDeviceExtendedAutoUpdateEnabled());
}

TEST_F(DevicePolicyImplTest, GetDeviceExtendedAutoUpdateEnabled_Unset) {
  device_policy_proto_.clear_deviceextendedautoupdateenabled();

  InitializePolicyForEnterprise();

  EXPECT_FALSE(device_policy_.GetDeviceExtendedAutoUpdateEnabled());
}

// Test that the policy is loaded only if the request token is present.
TEST_F(DevicePolicyImplTest, LoadPolicyRequestTokenPresenceCases) {
  InitializePolicyForEnterprise();
  device_policy_.set_verify_policy_for_testing(false);

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath file_path(temp_dir.GetPath().Append("policy"));
  device_policy_.set_policy_path_for_testing(file_path);
  base::File file(file_path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);

  // Create policy file without request token.
  em::ChromeDeviceSettingsProto device_policy_proto;
  em::PolicyFetchResponse policy_response;
  em::PolicyData policy_data;
  policy_data.set_policy_value(device_policy_proto_.SerializeAsString());
  policy_response.set_policy_data(policy_data.SerializeAsString());
  std::string data = policy_response.SerializeAsString();
  file.Write(0, data.c_str(), data.length());
  ASSERT_FALSE(device_policy_.LoadPolicy(/*delete_invalid_files=*/false));

  // Create policy file with request token.
  policy_data.set_request_token("1234");
  policy_response.set_policy_data(policy_data.SerializeAsString());
  data = policy_response.SerializeAsString();
  file.Write(0, data.c_str(), data.length());
  ASSERT_TRUE(device_policy_.LoadPolicy(/*delete_invalid_files=*/false));
}

TEST_F(DevicePolicyImplTest, MetricsEnabledReturnsTrueIfTrueIsSet) {
  device_policy_proto_.mutable_metrics_enabled()->set_metrics_enabled(true);

  InitializePolicyForEnterprise();

  EXPECT_THAT(device_policy_.GetMetricsEnabled(), testing::Optional(true));
}

TEST_F(DevicePolicyImplTest, MetricsEnabledReturnsFalseIfFalseIsSet) {
  device_policy_proto_.mutable_metrics_enabled()->set_metrics_enabled(false);

  InitializePolicyForEnterprise();

  EXPECT_THAT(device_policy_.GetMetricsEnabled(), testing::Optional(false));
}

TEST_F(DevicePolicyImplTest, MetricsEnabledDefaultsToTrueOnEnterpriseManaged) {
  policy_data_.set_management_mode(em::PolicyData::ENTERPRISE_MANAGED);
  InitializePolicyForEnterprise();

  EXPECT_THAT(device_policy_.GetMetricsEnabled(), testing::Optional(true));
}

TEST_F(DevicePolicyImplTest, MetricsEnabledDefaultsIsUnsetIfNotManaged) {
  device_policy_proto_.clear_metrics_enabled();
  InitializePolicyForConsumer();

  EXPECT_EQ(device_policy_.GetMetricsEnabled(), std::nullopt);
}

}  // namespace policy
