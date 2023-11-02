//
// Copyright 2022 The Android Open Source Project
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
#include "update_engine/update_manager/out_of_box_experience_policy_impl.h"
#include "update_engine/update_manager/policy_test_utils.h"

using chromeos_update_engine::FakeSystemState;

namespace chromeos_update_manager {

class UmOutOfBoxExperiencePolicyImplTest : public UmPolicyTestBase {
 protected:
  UmOutOfBoxExperiencePolicyImplTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCheckAllowedPolicyData());
    policy_2_.reset(new OobePolicyImpl());

    ucp_ =
        UpdateCheckAllowedPolicyData::GetUpdateCheckParams(policy_data_.get());
  }

  void SetUp() override {
    UmPolicyTestBase::SetUp();
    FakeSystemState::CreateInstance();
    FakeSystemState::Get()->set_prefs(nullptr);
  }

  UpdateCheckParams* ucp_;
};

TEST_F(UmOutOfBoxExperiencePolicyImplTest, ContinueForNonUpdates) {
  fake_state_.system_provider()->var_is_updating()->reset(new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
