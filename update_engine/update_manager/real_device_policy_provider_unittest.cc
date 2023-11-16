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

#include "update_engine/update_manager/real_device_policy_provider.h"

#include <memory>
#include <vector>

#include <base/memory/ptr_util.h>
#include <base/version.h>
#include <base/strings/stringprintf.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h>
#include <policy/device_policy.h>
#include <policy/mock_device_policy.h>
#include <policy/mock_libpolicy.h>
#include <session_manager/dbus-proxies.h>
#include <session_manager/dbus-proxy-mocks.h>

#include "update_engine/common/test_utils.h"
#include "update_engine/cros/dbus_test_utils.h"
#include "update_engine/update_manager/umtest_utils.h"

using brillo::MessageLoop;
using chromeos_update_engine::ConnectionType;
using chromeos_update_engine::dbus_test_utils::MockSignalHandler;
using policy::DevicePolicy;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgPointee;

namespace chromeos_update_manager {

namespace {
const char kDeviceVersionLsbRelease[] = "15183.1.2";
const char kTargetVersionPolicy[] = "12345.";
const char kInvalidTargetVersionPolicy[] = ".";

const base::Version kTargetVersion("12345.0.0");
const base::Version kDeviceVersion("15183.1.2");
const base::Version kDeviceVersionOld("15678.5.6");
const base::Version kTargetVersionOld("18950.7.9");
}  // namespace

class UmRealDevicePolicyProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_.SetAsCurrent();
    auto session_manager_proxy_mock =
        new org::chromium::SessionManagerInterfaceProxyMock();
    auto rollback_metrics = std::make_unique<
        oobe_config::EnterpriseRollbackMetricsHandlerForTesting>();
    rollback_metrics_ = rollback_metrics.get();
    provider_.reset(new RealDevicePolicyProvider(
        base::WrapUnique(session_manager_proxy_mock), &mock_policy_provider_,
        std::move(rollback_metrics)));
    // By default, we have a device policy loaded. Tests can call
    // SetUpNonExistentDevicePolicy() to override this.
    SetUpExistentDevicePolicy();

    // Setup the session manager_proxy such that it will accept the signal
    // handler and store it in the |property_change_complete_| once registered.
    MOCK_SIGNAL_HANDLER_EXPECT_SIGNAL_HANDLER(property_change_complete_,
                                              *session_manager_proxy_mock,
                                              PropertyChangeComplete);
    // Enable metrics reporting for rollback tracking by default.
    EXPECT_TRUE(rollback_metrics_->EnableMetrics());
  }

  void TearDown() override {
    provider_.reset();
    // Check for leaked callbacks on the main loop.
    EXPECT_FALSE(loop_.PendingTasks());
  }

  void SetUpNonExistentDevicePolicy() {
    ON_CALL(mock_policy_provider_, Reload()).WillByDefault(Return(false));
    ON_CALL(mock_policy_provider_, device_policy_is_loaded())
        .WillByDefault(Return(false));
    EXPECT_CALL(mock_policy_provider_, GetDevicePolicy()).Times(0);
  }

  void SetUpExistentDevicePolicy() {
    // Setup the default behavior of the mocked PolicyProvider.
    ON_CALL(mock_policy_provider_, Reload()).WillByDefault(Return(true));
    ON_CALL(mock_policy_provider_, device_policy_is_loaded())
        .WillByDefault(Return(true));
    ON_CALL(mock_policy_provider_, GetDevicePolicy())
        .WillByDefault(ReturnRef(mock_device_policy_));
  }

  brillo::FakeMessageLoop loop_{nullptr};
  testing::NiceMock<policy::MockDevicePolicy> mock_device_policy_;
  testing::NiceMock<policy::MockPolicyProvider> mock_policy_provider_;
  unique_ptr<RealDevicePolicyProvider> provider_;
  oobe_config::EnterpriseRollbackMetricsHandlerForTesting* rollback_metrics_;
  // Need to keep the variable around for enterprise rollback tracking tests.
  base::test::ScopedChromeOSVersionInfo version_info_{
      base::StringPrintf("CHROMEOS_RELEASE_VERSION=%s",
                         kDeviceVersionLsbRelease),
      base::Time()};

  // The registered signal handler for the signal.
  MockSignalHandler<void(const string&)> property_change_complete_;
};

TEST_F(UmRealDevicePolicyProviderTest, RefreshScheduledTest) {
  // Check that the RefreshPolicy gets scheduled by checking the TaskId.
  EXPECT_TRUE(provider_->Init());
  EXPECT_NE(MessageLoop::kTaskIdNull, provider_->scheduled_refresh_);
  loop_.RunOnce(false);
}

TEST_F(UmRealDevicePolicyProviderTest, FirstReload) {
  // Checks that the policy is reloaded and the DevicePolicy is consulted twice:
  // once on Init() and once again when the signal is connected.
  EXPECT_CALL(mock_policy_provider_, Reload());
  EXPECT_TRUE(provider_->Init());
  Mock::VerifyAndClearExpectations(&mock_policy_provider_);
  // We won't be notified that signal is connected without DBus.
  EXPECT_CALL(mock_policy_provider_, Reload());
  loop_.RunOnce(false);
}

TEST_F(UmRealDevicePolicyProviderTest, NonExistentDevicePolicyReloaded) {
  // Checks that the policy is reloaded by RefreshDevicePolicy().
  SetUpNonExistentDevicePolicy();
  // We won't be notified that signal is connected without DBus.
  EXPECT_CALL(mock_policy_provider_, Reload()).Times(3);
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  // Force the policy refresh.
  provider_->RefreshDevicePolicy();
}

TEST_F(UmRealDevicePolicyProviderTest, SessionManagerSignalForcesReload) {
  // Checks that a signal from the SessionManager forces a reload.
  SetUpNonExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, Reload()).Times(2);
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  Mock::VerifyAndClearExpectations(&mock_policy_provider_);

  EXPECT_CALL(mock_policy_provider_, Reload());
  ASSERT_TRUE(property_change_complete_.IsHandlerRegistered());
  property_change_complete_.signal_callback().Run("success");
}

TEST_F(UmRealDevicePolicyProviderTest, NonExistentDevicePolicyEmptyVariables) {
  SetUpNonExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, GetDevicePolicy()).Times(0);
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(false,
                                      provider_->var_device_policy_is_loaded());

  UmTestUtils::ExpectVariableNotSet(provider_->var_release_channel());
  UmTestUtils::ExpectVariableNotSet(provider_->var_release_channel_delegated());
  UmTestUtils::ExpectVariableNotSet(provider_->var_release_lts_tag());
  UmTestUtils::ExpectVariableNotSet(provider_->var_update_disabled());
  UmTestUtils::ExpectVariableNotSet(provider_->var_target_version_prefix());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_rollback_allowed_milestones());
  UmTestUtils::ExpectVariableNotSet(provider_->var_scatter_factor());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_allowed_connection_types_for_update());
  UmTestUtils::ExpectVariableNotSet(provider_->var_has_owner());
  UmTestUtils::ExpectVariableNotSet(provider_->var_http_downloads_enabled());
  UmTestUtils::ExpectVariableNotSet(provider_->var_au_p2p_enabled());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_allow_kiosk_app_control_chrome_version());
  UmTestUtils::ExpectVariableNotSet(provider_->var_disallowed_time_intervals());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_channel_downgrade_behavior());
  UmTestUtils::ExpectVariableNotSet(provider_->var_quick_fix_build_token());
  UmTestUtils::ExpectVariableNotSet(provider_->var_market_segment());
}

TEST_F(UmRealDevicePolicyProviderTest, ValuesUpdated) {
  SetUpNonExistentDevicePolicy();
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  Mock::VerifyAndClearExpectations(&mock_policy_provider_);

  // Reload the policy with a good one and set some values as present. The
  // remaining values are false.
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetReleaseChannel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string("mychannel")), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetAllowedConnectionTypesForUpdate(_))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_device_policy_, GetAllowKioskAppControlChromeVersion(_))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(true)));

  provider_->RefreshDevicePolicy();

  UmTestUtils::ExpectVariableHasValue(true,
                                      provider_->var_device_policy_is_loaded());

  // Test that at least one variable is set, to ensure the refresh occurred.
  UmTestUtils::ExpectVariableHasValue(string("mychannel"),
                                      provider_->var_release_channel());
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_allowed_connection_types_for_update());
  UmTestUtils::ExpectVariableHasValue(
      true, provider_->var_allow_kiosk_app_control_chrome_version());
}

TEST_F(UmRealDevicePolicyProviderTest, HasOwnerConverted) {
  SetUpExistentDevicePolicy();
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  Mock::VerifyAndClearExpectations(&mock_policy_provider_);

  EXPECT_CALL(mock_device_policy_, GetOwner(_))
      .Times(2)
      .WillOnce(DoAll(SetArgPointee<0>(string("")), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(string("abc@test.org")), Return(true)));

  // Enterprise enrolled device.
  provider_->RefreshDevicePolicy();
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_has_owner());

  // Has a device owner.
  provider_->RefreshDevicePolicy();
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_has_owner());
}

TEST_F(UmRealDevicePolicyProviderTest, RollbackToTargetVersionConverted) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(2), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndPowerwash,
      provider_->var_rollback_to_target_version());
}

TEST_F(UmRealDevicePolicyProviderTest,
       DoNoStartTrackingRollbackIfMetricsAreDisabled) {
  ASSERT_TRUE(rollback_metrics_->DisableMetrics());
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(3), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndRestoreIfPossible,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest, StartTrackingIfRollbackPoliciesAreSet) {
  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(3), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndRestoreIfPossible,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(rollback_metrics_->IsTrackingForDeviceVersion(kDeviceVersion));
  ASSERT_TRUE(rollback_metrics_->IsTrackingForTargetVersion(kTargetVersion));
}

TEST_F(UmRealDevicePolicyProviderTest, RestartTrackingIfTargetVersionChanges) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(3), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndRestoreIfPossible,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(rollback_metrics_->IsTrackingForDeviceVersion(kDeviceVersion));
  ASSERT_TRUE(rollback_metrics_->IsTrackingForTargetVersion(kTargetVersion));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(UmRealDevicePolicyProviderTest,
       DoNotRestartTrackingIfTargetVersionDoesNotChange) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersion));
  ASSERT_TRUE(rollback_metrics_->TrackEvent(
      oobe_config::EnterpriseRollbackMetricsHandler::CreateEventData(
          EnterpriseRollbackEvent::EVENT_UNSPECIFIED)));

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(3), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndRestoreIfPossible,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  // The content of the file has not been overwritten.
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  ASSERT_TRUE(rollback_metrics_->IsTrackingForDeviceVersion(kDeviceVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingForTargetVersion(kTargetVersion));
  ASSERT_EQ(1, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::EVENT_UNSPECIFIED));
  ASSERT_EQ(0, rollback_metrics_->TimesEventHasBeenTracked(
                   EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
}

TEST_F(UmRealDevicePolicyProviderTest, StopTrackingRollbackIfNoPolicies) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest,
       DoNotStopTrackingRollbackIfNoPoliciesButNotEnrolled) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest,
       StopTrackingRollbackIfPolicyDoesNotRestore) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(2), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndPowerwash,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest, StopTrackingRollbackIfPolicyIsNotValid) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(-1), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest,
       StopTrackingRollbackIfTargetVersionIsNotValid) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetRollbackToTargetVersion(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(3), Return(true)));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kInvalidTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableHasValue(
      RollbackToTargetVersion::kRollbackAndRestoreIfPossible,
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kInvalidTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest,
       StopTrackingRollbackIfNoRollbackPolicyButTargetVersion) {
  ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionOld,
                                                       kTargetVersionOld));
  ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());

  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_device_policy_, GetTargetVersionPrefix(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kTargetVersionPolicy), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_rollback_to_target_version());
  UmTestUtils::ExpectVariableHasValue(string(kTargetVersionPolicy),
                                      provider_->var_target_version_prefix());

  ASSERT_FALSE(rollback_metrics_->IsTrackingRollback());
}

TEST_F(UmRealDevicePolicyProviderTest, ScatterFactorConverted) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetScatterFactorInSeconds(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(1234), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(base::Seconds(1234),
                                      provider_->var_scatter_factor());
}

TEST_F(UmRealDevicePolicyProviderTest, NegativeScatterFactorIgnored) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetScatterFactorInSeconds(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(-1), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableNotSet(provider_->var_scatter_factor());
}

TEST_F(UmRealDevicePolicyProviderTest, AllowedTypesConverted) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetAllowedConnectionTypesForUpdate(_))
      .Times(2)
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(set<string>{"ethernet", "wifi", "not-a-type"}),
                Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(
      set<ConnectionType>{ConnectionType::kWifi, ConnectionType::kEthernet},
      provider_->var_allowed_connection_types_for_update());
}

TEST_F(UmRealDevicePolicyProviderTest, DisallowedIntervalsConverted) {
  SetUpExistentDevicePolicy();

  vector<DevicePolicy::WeeklyTimeInterval> intervals = {
      {5, base::Hours(5), 6, base::Hours(8)},
      {1, base::Hours(1), 3, base::Hours(10)}};

  EXPECT_CALL(mock_device_policy_, GetDisallowedTimeIntervals(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(intervals), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(
      WeeklyTimeIntervalVector{
          WeeklyTimeInterval(WeeklyTime(5, base::Hours(5)),
                             WeeklyTime(6, base::Hours(8))),
          WeeklyTimeInterval(WeeklyTime(1, base::Hours(1)),
                             WeeklyTime(3, base::Hours(10)))},
      provider_->var_disallowed_time_intervals());
}

TEST_F(UmRealDevicePolicyProviderTest, ChannelDowngradeBehaviorConverted) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetChannelDowngradeBehavior(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(static_cast<int>(
                                ChannelDowngradeBehavior::kRollback)),
                            Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(
      ChannelDowngradeBehavior::kRollback,
      provider_->var_channel_downgrade_behavior());
}

TEST_F(UmRealDevicePolicyProviderTest, ChannelDowngradeBehaviorTooSmall) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetChannelDowngradeBehavior(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(-1), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableNotSet(
      provider_->var_channel_downgrade_behavior());
}

TEST_F(UmRealDevicePolicyProviderTest, ChannelDowngradeBehaviorTooLarge) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetChannelDowngradeBehavior(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(10), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableNotSet(
      provider_->var_channel_downgrade_behavior());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceMinimumVersionPolicySet) {
  SetUpExistentDevicePolicy();

  base::Version device_minimum_version("13315.60.12");

  EXPECT_CALL(mock_device_policy_, GetHighestDeviceMinimumVersion(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(device_minimum_version), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(device_minimum_version,
                                      provider_->var_device_minimum_version());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceQuickFixBuildTokenSet) {
  SetUpExistentDevicePolicy();

  EXPECT_CALL(mock_device_policy_, GetDeviceQuickFixBuildToken(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(string("some_token")), Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(string("some_token"),
                                      provider_->var_quick_fix_build_token());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceSegmentEducation) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetDeviceMarketSegment(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(
                    policy::DevicePolicy::DeviceMarketSegment::kEducation),
                Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(string("education"),
                                      provider_->var_market_segment());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceSegmentEnterprise) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetDeviceMarketSegment(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(
                    policy::DevicePolicy::DeviceMarketSegment::kEnterprise),
                Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(string("enterprise"),
                                      provider_->var_market_segment());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceSegmentUnknown) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetDeviceMarketSegment(_))
      .WillRepeatedly(DoAll(
          SetArgPointee<0>(policy::DevicePolicy::DeviceMarketSegment::kUnknown),
          Return(true)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(string("unknown"),
                                      provider_->var_market_segment());
}

TEST_F(UmRealDevicePolicyProviderTest, DeviceSegmentNotSet) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_device_policy_, GetDeviceMarketSegment(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(
                    policy::DevicePolicy::DeviceMarketSegment::kEnterprise),
                Return(false)));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableNotSet(provider_->var_market_segment());
}

TEST_F(UmRealDevicePolicyProviderTest, IsEnterpriseEnrolledTrue) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(true,
                                      provider_->var_is_enterprise_enrolled());
}

TEST_F(UmRealDevicePolicyProviderTest, IsEnterpriseEnrolledFalse) {
  SetUpExistentDevicePolicy();
  EXPECT_CALL(mock_policy_provider_, IsEnterpriseEnrolledDevice())
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(provider_->Init());
  loop_.RunOnce(false);

  UmTestUtils::ExpectVariableHasValue(false,
                                      provider_->var_is_enterprise_enrolled());
}

}  // namespace chromeos_update_manager
