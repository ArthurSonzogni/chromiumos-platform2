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

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/enterprise_device_policy_impl.h"
#include "update_engine/update_manager/next_update_check_policy_impl.h"
#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/update_can_start_policy.h"
#include "update_engine/update_manager/update_check_allowed_policy.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"
#include "update_engine/update_manager/update_time_restrictions_policy_impl.h"
#include "update_engine/update_manager/weekly_time.h"

using base::Time;
using base::TimeDelta;
using std::string;

namespace chromeos_update_manager {

class UmUpdateCheckAllowedPolicyTest : public UmPolicyTestBase {
 protected:
  UmUpdateCheckAllowedPolicyTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCheckAllowedPolicyData());
    policy_2_.reset(new UpdateCheckAllowedPolicy());

    uca_data_ = static_cast<typeof(uca_data_)>(policy_data_.get());
  }

  void SetUp() override {
    UmPolicyTestBase::SetUp();
    SetUpDefaultDevicePolicy();
  }

  void SetUpDefaultState() override {
    UmPolicyTestBase::SetUpDefaultState();

    // OOBE is enabled by default.
    fake_state_.config_provider()->var_is_oobe_enabled()->reset(new bool(true));

    // For the purpose of the tests, this is an official build and OOBE was
    // completed.
    fake_state_.system_provider()->var_is_official_build()->reset(
        new bool(true));
    fake_state_.system_provider()->var_is_oobe_complete()->reset(
        new bool(true));
    // NOLINTNEXTLINE(readability/casting)
    fake_state_.system_provider()->var_num_slots()->reset(new unsigned int(2));
  }

  // Sets up a default device policy that does not impose any restrictions
  // (HTTP) nor enables any features (P2P).
  void SetUpDefaultDevicePolicy() {
    fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
        new bool(true));
    fake_state_.device_policy_provider()->var_update_disabled()->reset(
        new bool(false));
    fake_state_.device_policy_provider()
        ->var_allowed_connection_types_for_update()
        ->reset(nullptr);
    fake_state_.device_policy_provider()->var_scatter_factor()->reset(
        new TimeDelta());
    fake_state_.device_policy_provider()->var_http_downloads_enabled()->reset(
        new bool(true));
    fake_state_.device_policy_provider()->var_au_p2p_enabled()->reset(
        new bool(false));
    fake_state_.device_policy_provider()
        ->var_release_channel_delegated()
        ->reset(new bool(true));
    fake_state_.device_policy_provider()
        ->var_disallowed_time_intervals()
        ->reset(new WeeklyTimeIntervalVector());
  }

  // Configures the policy to return a desired value from UpdateCheckAllowed by
  // faking the current wall clock time as needed. Restores the default state.
  // This is used when testing policies that depend on this one.
  //
  // Note that the default implementation relies on NextUpdateCheckPolicyImpl to
  // set the FakeClock to the appropriate time.
  virtual void SetUpdateCheckAllowed(bool allow_check) {
    Time next_update_check;
    CallMethodWithContext(&NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
                          &next_update_check,
                          kNextUpdateCheckPolicyConstants);
    SetUpDefaultState();
    SetUpDefaultDevicePolicy();
    Time curr_time = next_update_check;
    if (allow_check)
      curr_time += base::Seconds(1);
    else
      curr_time -= base::Seconds(1);
    fake_clock_->SetWallclockTime(curr_time);
  }

  UpdateCheckAllowedPolicyData* uca_data_;
};

TEST_F(UmUpdateCheckAllowedPolicyTest, UpdateCheckAllowedWaitsForTheTimeout) {
  // We get the next update_check timestamp from the policy's private method
  // and then we check the public method respects that value on the normal
  // case.
  Time next_update_check;
  Time last_checked_time =
      fake_clock_->GetWallclockTime() + base::Minutes(1234);

  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  CallMethodWithContext(&NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
                        &next_update_check,
                        kNextUpdateCheckPolicyConstants);

  // Check that the policy blocks until the next_update_check is reached.
  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_->SetWallclockTime(next_update_check - base::Seconds(1));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());

  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_->SetWallclockTime(next_update_check + base::Seconds(1));
  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest, UpdateCheckAllowedWaitsForOOBE) {
  // Update checks are deferred until OOBE is completed.

  // Ensure that update is not allowed even if wait period is satisfied.
  Time next_update_check;
  Time last_checked_time =
      fake_clock_->GetWallclockTime() + base::Minutes(1234);

  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  CallMethodWithContext(&NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
                        &next_update_check,
                        kNextUpdateCheckPolicyConstants);

  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_->SetWallclockTime(next_update_check + base::Seconds(1));
  fake_state_.system_provider()->var_is_oobe_complete()->reset(new bool(false));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());

  // Now check that it is allowed if OOBE is completed.
  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_->SetWallclockTime(next_update_check + base::Seconds(1));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest, UpdateCheckAllowedWithAttributes) {
  // Update check is allowed, response includes attributes for use in the
  // request.
  SetUpdateCheckAllowed(true);

  // Override specific device policy attributes.
  fake_state_.device_policy_provider()->var_target_version_prefix()->reset(
      new string("1.2"));
  fake_state_.device_policy_provider()
      ->var_rollback_allowed_milestones()
      ->reset(new int(5));
  fake_state_.device_policy_provider()->var_release_channel_delegated()->reset(
      new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->reset(
      new string("foo-channel"));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_EQ("1.2", uca_data_->update_check_params.target_version_prefix);
  EXPECT_EQ("foo-channel", uca_data_->update_check_params.target_channel);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedUpdatesDisabledForUnofficialBuilds) {
  // UpdateCheckAllowed should return kAskMeAgainLater if this is an unofficial
  // build; we don't want periodic update checks on developer images.

  fake_state_.system_provider()->var_is_official_build()->reset(
      new bool(false));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmUpdateCheckAllowedPolicyTest, TestUpdateCheckIntervalTimeout) {
  fake_state_.updater_provider()
      ->var_test_update_check_interval_timeout()
      ->reset(new int64_t(10));
  fake_state_.system_provider()->var_is_official_build()->reset(
      new bool(false));

  // The first time, update should not be allowed.
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());

  // After moving the time forward more than the update check interval, it
  // should now allow for update.
  fake_clock_->SetWallclockTime(fake_clock_->GetWallclockTime() +
                                base::Seconds(11));
  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedUpdatesDisabledWhenNotEnoughSlotsAbUpdates) {
  // UpdateCheckAllowed should return false (kSucceeded) if the image booted
  // without enough slots to do A/B updates.

  // NOLINTNEXTLINE(readability/casting)
  fake_state_.system_provider()->var_num_slots()->reset(new unsigned int(1));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_FALSE(uca_data_->update_check_params.updates_enabled);
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedUpdatesDisabledByPolicy) {
  // UpdateCheckAllowed should return kAskMeAgainLater because a device policy
  // is loaded and prohibits updates.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedForcedUpdateRequestedInteractive) {
  // UpdateCheckAllowed should return true because a forced update request was
  // signaled for an interactive update.

  SetUpdateCheckAllowed(true);
  fake_state_.updater_provider()->var_forced_update_requested()->reset(
      new UpdateRequestStatus(UpdateRequestStatus::kInteractive));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_TRUE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedForcedUpdateRequestedPeriodic) {
  // UpdateCheckAllowed should return true because a forced update request was
  // signaled for a periodic check.

  SetUpdateCheckAllowed(true);
  fake_state_.updater_provider()->var_forced_update_requested()->reset(
      new UpdateRequestStatus(UpdateRequestStatus::kPeriodic));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest, UpdateCheckAllowedConsumerEnabled) {
  // Needed to allow next update check to pass the interval check.
  SetUpdateCheckAllowed(true);
  fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
      new bool(false));
  fake_state_.updater_provider()->var_consumer_auto_update_disabled()->reset(
      new bool(false));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest, UpdateCheckAllowedConsumerDisabled) {
  // Can be omitted, but allows that consumer update is actually what led to
  // `kAskMeAgainLater`.
  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
      new bool(false));
  fake_state_.updater_provider()->var_consumer_auto_update_disabled()->reset(
      new bool(true));

  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
  EXPECT_FALSE(uca_data_->update_check_params.interactive);
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedInstallationsWhenBootedFromNonABSlots) {
  // Even if there aren't enough slots, an installation should fall through.
  // NOLINTNEXTLINE(readability/casting)
  fake_state_.system_provider()->var_num_slots()->reset(new unsigned int(1));
  fake_state_.system_provider()->var_is_updating()->reset(new bool(false));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
}

TEST_F(UmUpdateCheckAllowedPolicyTest,
       UpdateCheckAllowedInstallationsWhenEntDisablesUpdates) {
  // Even if device policy blocks updates, an installation should fall through.
  fake_state_.system_provider()->var_is_updating()->reset(new bool(false));

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(uca_data_->update_check_params.updates_enabled);
}

}  // namespace chromeos_update_manager
