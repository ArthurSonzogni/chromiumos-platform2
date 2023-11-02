//
// Copyright (C) 2018 The Android Open Source Project
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

#include "update_engine/update_manager/update_time_restrictions_policy_impl.h"

#include <memory>

#include <base/time/time.h>

#include "update_engine/update_manager/policy_test_utils.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/weekly_time.h"

using base::Time;
using base::TimeDelta;
using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::InstallPlan;
using std::string;

namespace chromeos_update_manager {

constexpr TimeDelta kHour = base::Hours(1);
constexpr TimeDelta kMinute = base::Minutes(1);

const WeeklyTimeIntervalVector kTestIntervals{
    // Monday 10:15 AM to Monday 3:30 PM.
    WeeklyTimeInterval(WeeklyTime(1, kHour * 10 + kMinute * 15),
                       WeeklyTime(1, kHour * 15 + kMinute * 30)),
    // Wednesday 8:30 PM to Thursday 8:40 AM.
    WeeklyTimeInterval(WeeklyTime(3, kHour * 20 + kMinute * 30),
                       WeeklyTime(4, kHour * 8 + kMinute * 40)),
};

class UmUpdateTimeRestrictionsPolicyImplTest : public UmPolicyTestBase {
 protected:
  UmUpdateTimeRestrictionsPolicyImplTest() {
    policy_data_.reset(new UpdateCanBeAppliedPolicyData(&install_plan_));
    policy_2_.reset(new UpdateTimeRestrictionsPolicyImpl());

    ucba_data_ = static_cast<typeof(ucba_data_)>(policy_data_.get());
  }

  void TestPolicy(const Time& current_time,
                  const WeeklyTimeIntervalVector& test_intervals,
                  const EvalStatus& expected_value,
                  bool expected_can_download_be_canceled) {
    fake_clock_->SetWallclockTime(current_time);
    SetUpDefaultTimeProvider();
    fake_state_.device_policy_provider()
        ->var_disallowed_time_intervals()
        ->reset(new WeeklyTimeIntervalVector(test_intervals));

    EXPECT_EQ(expected_value, evaluator_->Evaluate());
    EXPECT_EQ(install_plan_.can_download_be_canceled,
              expected_can_download_be_canceled);
    if (expected_value == EvalStatus::kSucceeded)
      EXPECT_EQ(ucba_data_->error_code(),
                ErrorCode::kOmahaUpdateDeferredPerPolicy);
  }

  InstallPlan install_plan_;
  UpdateCanBeAppliedPolicyData* ucba_data_;
};

// If there are no intervals, then the policy should always return |kContinue|.
TEST_F(UmUpdateTimeRestrictionsPolicyImplTest, NoIntervalsSetTest) {
  TestPolicy(base::Time::Now(),
             WeeklyTimeIntervalVector(),
             EvalStatus::kContinue,
             /*expected_can_download_be_canceled=*/true);
}

// Check that all intervals are checked.
TEST_F(UmUpdateTimeRestrictionsPolicyImplTest, TimeInRange) {
  // Monday, July 9th 2018 12:30 PM.
  Time::Exploded first_interval_time{2018, 7, 1, 9, 12, 30, 0, 0};
  Time time;
  EXPECT_TRUE(Time::FromLocalExploded(first_interval_time, &time));
  TestPolicy(time,
             kTestIntervals,
             EvalStatus::kSucceeded,
             /*expected_can_download_be_canceled=*/true);

  // Check second interval.
  // Thursday, July 12th 2018 4:30 AM.
  Time::Exploded second_interval_time{2018, 7, 4, 12, 4, 30, 0, 0};
  EXPECT_TRUE(Time::FromLocalExploded(second_interval_time, &time));
  TestPolicy(time,
             kTestIntervals,
             EvalStatus::kSucceeded,
             /*expected_can_download_be_canceled=*/true);
}

// If the current time is out of restrictions, then the policy should always
// return |kContinue|.
TEST_F(UmUpdateTimeRestrictionsPolicyImplTest, TimeOutOfRange) {
  // Monday, July 9th 2018 6:30 PM.
  Time time;
  Time::Exploded out_of_range_time{2018, 7, 1, 9, 18, 30, 0, 0};
  EXPECT_TRUE(Time::FromLocalExploded(out_of_range_time, &time));
  TestPolicy(time,
             kTestIntervals,
             EvalStatus::kContinue,
             /*expected_can_download_be_canceled=*/true);
}

// If the quick fix build token is set, then the policy should always return
// |kContinue|.
TEST_F(UmUpdateTimeRestrictionsPolicyImplTest, QuickFixBuildToken) {
  fake_state_.device_policy_provider()->var_quick_fix_build_token()->reset(
      new std::string("foo-token"));
  TestPolicy(base::Time::Now(),
             kTestIntervals,
             EvalStatus::kContinue,
             /*expected_can_download_be_canceled=*/false);
}

}  // namespace chromeos_update_manager
