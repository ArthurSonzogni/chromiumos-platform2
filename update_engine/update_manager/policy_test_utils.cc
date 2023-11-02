//
// Copyright (C) 2017 The Android Open Source Project
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

#include "update_engine/update_manager/policy_test_utils.h"

#include <memory>
#include <utility>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/next_update_check_policy_impl.h"

using base::Time;
using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::FakeSystemState;

namespace chromeos_update_manager {

void UmPolicyTestBase::SetUp() {
  loop_.SetAsCurrent();
  FakeSystemState::CreateInstance();
  fake_clock_ = FakeSystemState::Get()->fake_clock();
  SetUpDefaultClock();
  eval_ctx_.reset(new EvaluationContext(base::Seconds(5)));
  SetUpDefaultState();

  evaluator_ = std::make_unique<PolicyEvaluator>(
      &fake_state_,
      std::make_unique<EvaluationContext>(base::Seconds(5)),
      std::move(policy_2_),
      policy_data_);
}

void UmPolicyTestBase::TearDown() {
  EXPECT_FALSE(loop_.PendingTasks());
}

// Sets the clock to fixed values.
void UmPolicyTestBase::SetUpDefaultClock() {
  fake_clock_->SetMonotonicTime(Time::FromInternalValue(12345678L));
  fake_clock_->SetWallclockTime(Time::FromInternalValue(12345678901234L));
}

void UmPolicyTestBase::SetUpDefaultTimeProvider() {
  Time current_time = FakeSystemState::Get()->clock()->GetWallclockTime();
  base::Time::Exploded exploded;
  current_time.LocalExplode(&exploded);
  fake_state_.time_provider()->var_curr_hour()->reset(new int(exploded.hour));
  fake_state_.time_provider()->var_curr_minute()->reset(
      new int(exploded.minute));
  fake_state_.time_provider()->var_curr_date()->reset(
      new Time(current_time.LocalMidnight()));
}

void UmPolicyTestBase::SetUpDefaultState() {
  fake_state_.updater_provider()->var_updater_started_time()->reset(
      new Time(fake_clock_->GetWallclockTime()));
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(fake_clock_->GetWallclockTime()));
  fake_state_.updater_provider()->var_consecutive_failed_update_checks()->reset(
      new unsigned int(0));  // NOLINT(readability/casting)
  fake_state_.updater_provider()->var_server_dictated_poll_interval()->reset(
      new unsigned int(0));  // NOLINT(readability/casting)
  fake_state_.updater_provider()->var_forced_update_requested()->reset(
      new UpdateRequestStatus{UpdateRequestStatus::kNone});

  // Chosen by fair dice roll.  Guaranteed to be random.
  fake_state_.random_provider()->var_seed()->reset(new uint64_t(4));
}

}  // namespace chromeos_update_manager
