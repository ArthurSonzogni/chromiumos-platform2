// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/recovery_policy.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"

namespace chromeos_update_manager {

class UmRecoveryPolicyTest : public UmPolicyTestBase {
 protected:
  UmRecoveryPolicyTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCheckAllowedPolicyData());
    policy_2_.reset(new RecoveryPolicy());
  }
};

TEST_F(UmRecoveryPolicyTest, RecoveryModeInteractive) {
  fake_state_.config_provider()->var_is_running_from_minios()->reset(
      new bool(true));
  // Allow interactive updates to happen.
  fake_state_.updater_provider()->var_forced_update_requested()->reset(
      new UpdateRequestStatus{UpdateRequestStatus::kInteractive});
  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
}

TEST_F(UmRecoveryPolicyTest, RecoveryModeNonInteractive) {
  fake_state_.config_provider()->var_is_running_from_minios()->reset(
      new bool(true));
  // Ignore a non interactive update.
  fake_state_.updater_provider()->var_forced_update_requested()->reset(
      new UpdateRequestStatus{UpdateRequestStatus::kNone});
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

TEST_F(UmRecoveryPolicyTest, NotRecoveryMode) {
  fake_state_.config_provider()->var_is_running_from_minios()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
