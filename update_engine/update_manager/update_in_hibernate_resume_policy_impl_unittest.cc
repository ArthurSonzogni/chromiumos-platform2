//
// Copyright (C) 2022 The Android Open Source Project
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

#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/update_manager/real_time_provider.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/update_in_hibernate_resume_policy_impl.h"

#include <memory>

#include <base/time/time.h>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/policy_test_utils.h"

using base::Time;
using chromeos_update_engine::FakeSystemState;
using chromeos_update_engine::InstallPlan;

namespace chromeos_update_manager {

class UmUpdateInHibernateResumePolicyImplTest : public UmPolicyTestBase {
 protected:
  UmUpdateInHibernateResumePolicyImplTest() : UmPolicyTestBase() {
    policy_data_.reset(new UpdateCanBeAppliedPolicyData(&install_plan_));
    policy_2_.reset(new UpdateInHibernateResumePolicyImpl());
  }

  void SetUp() override {
    const Time now = Time::Now();
    UmPolicyTestBase::SetUp();
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(now);
    fake_state_.updater_provider()->var_updater_started_time()->reset(
        new Time(now));
  }

  InstallPlan install_plan_;
};

// If resume is not pending, the policy should continue.
TEST_F(UmUpdateInHibernateResumePolicyImplTest, NonResumeContinues) {
  fake_state_.system_provider()->var_is_resuming_from_hibernate()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

// In resume, return |kAskMeAgainLater|.
TEST_F(UmUpdateInHibernateResumePolicyImplTest, ResumeDelaysUpdate) {
  fake_state_.system_provider()->var_is_resuming_from_hibernate()->reset(
      new bool(true));
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

// After awhile, the policy still allows updates to continue, assuming resume
// can be successfully aborted.
TEST_F(UmUpdateInHibernateResumePolicyImplTest, UpdatesEventuallyContinue) {
  Time the_past = Time::Now() - kMaxHibernateResumeTime - base::Seconds(2);
  fake_state_.updater_provider()->var_updater_started_time()->reset(
      new Time(the_past));
  fake_state_.system_provider()->var_is_resuming_from_hibernate()->reset(
      new bool(true));
  fake_state_.system_provider()->var_abort_resume_from_hibernate()->reset(
      new bool(true));
  EXPECT_EQ(EvalStatus::kContinue, evaluator_->Evaluate());
}

// Updates should still be deferred if the timeout expires but the attempt to
// abort fails.
TEST_F(UmUpdateInHibernateResumePolicyImplTest,
       UpdatesFailInFailedAbortResume) {
  Time the_past = Time::Now() - kMaxHibernateResumeTime - base::Seconds(2);
  fake_state_.updater_provider()->var_updater_started_time()->reset(
      new Time(the_past));
  fake_state_.system_provider()->var_is_resuming_from_hibernate()->reset(
      new bool(true));
  fake_state_.system_provider()->var_abort_resume_from_hibernate()->reset(
      new bool(false));
  EXPECT_EQ(EvalStatus::kAskMeAgainLater, evaluator_->Evaluate());
}

}  // namespace chromeos_update_manager
