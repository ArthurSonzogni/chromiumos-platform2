// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/enterprise_update_disabled_policy_impl.h"

#include "update_engine/update_manager/policy_test_utils.h"

namespace chromeos_update_manager {

class UmEnterpriseUpdateDisabledPolicyImplTest : public UmPolicyTestBase {
 protected:
  UmEnterpriseUpdateDisabledPolicyImplTest() : UmPolicyTestBase() {
    policy_2_.reset(new EnterpriseUpdateDisabledPolicyImpl);
  }
};

TEST_F(UmEnterpriseUpdateDisabledPolicyImplTest,
       ContinueIfEnterpriseConsumerUnset) {
  fake_state_.device_policy_provider()->var_is_enterprise_enrolled()->reset(
      new bool());
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseUpdateDisabledPolicyImplTest,
       ContinueIfNotEnterpriseConsumer) {
  fake_state_.device_policy_provider()->var_is_enterprise_enrolled()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseUpdateDisabledPolicyImplTest, AskAgainIfUpdatesEnabled) {
  fake_state_.device_policy_provider()->var_is_enterprise_enrolled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseUpdateDisabledPolicyImplTest,
       AskAgainIfUpdatesEnabledUnset) {
  fake_state_.device_policy_provider()->var_is_enterprise_enrolled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool());
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseUpdateDisabledPolicyImplTest, SucceedIfUpdatesDisabled) {
  fake_state_.device_policy_provider()->var_is_enterprise_enrolled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));
  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
