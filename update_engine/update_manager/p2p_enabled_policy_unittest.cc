// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/p2p_enabled_policy.h"
#include "update_engine/update_manager/policy_test_utils.h"

namespace chromeos_update_manager {

class UmP2PEnabledPolicyTest : public UmPolicyTestBase {
 protected:
  UmP2PEnabledPolicyTest() : UmPolicyTestBase() {
    policy_data_.reset(new P2PEnabledPolicyData());
    policy_2_.reset(new P2PEnabledPolicy());

    p2p_data_ = static_cast<typeof(p2p_data_)>(policy_data_.get());
  }

  void SetUp() override {
    UmPolicyTestBase::SetUp();
    fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
        new bool(true));
    fake_state_.device_policy_provider()->var_has_owner()->reset(
        new bool(true));
  }

  P2PEnabledPolicyData* p2p_data_;
};

TEST_F(UmP2PEnabledPolicyTest, NotAllowed) {
  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_FALSE(p2p_data_->enabled());
}

TEST_F(UmP2PEnabledPolicyTest, AllowedByDevicePolicy) {
  fake_state_.device_policy_provider()->var_au_p2p_enabled()->reset(
      new bool(true));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(p2p_data_->enabled());
}

TEST_F(UmP2PEnabledPolicyTest, AllowedByUpdater) {
  fake_state_.updater_provider()->var_p2p_enabled()->reset(new bool(true));

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_TRUE(p2p_data_->enabled());
}

class UmP2PEnabledChangedPolicyTest : public UmPolicyTestBase {
 protected:
  UmP2PEnabledChangedPolicyTest() : UmPolicyTestBase() {
    policy_data_.reset(new P2PEnabledPolicyData());
    policy_2_.reset(new P2PEnabledChangedPolicy());
  }
};

TEST_F(UmP2PEnabledChangedPolicyTest, Blocks) {
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
