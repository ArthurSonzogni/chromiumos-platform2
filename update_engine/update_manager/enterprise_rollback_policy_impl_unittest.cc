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
