// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "update_engine/update_manager/enterprise_rollback_policy_impl.h"
#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/weekly_time.h"

using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::InstallPlan;

namespace chromeos_update_manager {

class UmEnterpriseRollbackPolicyImplTest : public UmPolicyTestBase {
 protected:
  UmEnterpriseRollbackPolicyImplTest() {
    policy_data_.reset(new UpdateCanBeAppliedPolicyData(&install_plan_));
    policy_2_.reset(new EnterpriseRollbackPolicyImpl());

    ucba_data_ = static_cast<typeof(ucba_data_)>(policy_data_.get());
  }

  InstallPlan install_plan_;
  UpdateCanBeAppliedPolicyData* ucba_data_;
};

TEST_F(UmEnterpriseRollbackPolicyImplTest,
       ContinueWhenUpdateIsNotEnterpriseRollback) {
  install_plan_.is_rollback = false;

  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

TEST_F(UmEnterpriseRollbackPolicyImplTest,
       SuccessWhenUpdateIsEnterpriseRollback) {
  install_plan_.is_rollback = true;

  EXPECT_EQ(EvalStatus::kSucceeded, evaluator_->Evaluate());
  EXPECT_EQ(ucba_data_->error_code(), ErrorCode::kSuccess);
}

}  // namespace chromeos_update_manager
