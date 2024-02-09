// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/update_manager/deferred_update_policy_impl.h"
#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"

using chromeos_update_engine::DeferUpdateAction;
using chromeos_update_engine::FakeSystemState;
using chromeos_update_engine::InstallPlan;
using testing::_;
using testing::Return;

namespace chromeos_update_manager {

class UmDeferredUpdatePolicyImplTest : public UmPolicyTestBase {
 protected:
  UmDeferredUpdatePolicyImplTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCanBeAppliedPolicyData(&install_plan_));
    policy_2_.reset(new DeferredUpdatePolicyImpl());
  }

  void SetUp() override {
    UmPolicyTestBase::SetUp();
    FakeSystemState::CreateInstance();
    FakeSystemState::Get()->set_prefs(nullptr);
  }

  InstallPlan install_plan_;
};

TEST_F(UmDeferredUpdatePolicyImplTest, SkipIfDevicePolicyExists) {
  fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
      new bool(true));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(DeferUpdateAction::kOff, install_plan_.defer_update_action);
}

TEST_F(UmDeferredUpdatePolicyImplTest, SkipIfNotDisabled) {
  fake_state_.device_policy_provider()->var_has_owner()->reset(new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(DeferUpdateAction::kOff, install_plan_.defer_update_action);
}

TEST_F(UmDeferredUpdatePolicyImplTest, ConsumerDeviceEnabledAutoUpdate) {
  fake_state_.device_policy_provider()->var_has_owner()->reset(new bool(true));
  fake_state_.updater_provider()->var_consumer_auto_update_disabled()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(DeferUpdateAction::kOff, install_plan_.defer_update_action);
}

TEST_F(UmDeferredUpdatePolicyImplTest, ConsumerDeviceDisabledAutoUpdate) {
  fake_state_.device_policy_provider()->var_has_owner()->reset(new bool(true));
  fake_state_.updater_provider()->var_consumer_auto_update_disabled()->reset(
      new bool(true));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(DeferUpdateAction::kHold, install_plan_.defer_update_action);
}

TEST_F(UmDeferredUpdatePolicyImplTest, ManagedDeviceContinues) {
  fake_state_.device_policy_provider()->var_has_owner()->reset(new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
  EXPECT_EQ(DeferUpdateAction::kOff, install_plan_.defer_update_action);
}

}  // namespace chromeos_update_manager
