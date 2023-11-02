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

#include <base/logging.h>
#include <base/time/time.h>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/system_provider.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/weekly_time.h"

using base::Time;
using chromeos_update_engine::ErrorCode;
using std::string;

namespace chromeos_update_manager {

EvalStatus UpdateTimeRestrictionsPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  DevicePolicyProvider* const dp_provider = state->device_policy_provider();

  const string* quick_fix_build_token_p =
      ec->GetValue(dp_provider->var_quick_fix_build_token());
  if (quick_fix_build_token_p != nullptr && !quick_fix_build_token_p->empty()) {
    LOG(INFO) << "Quick fix build token found - Skip update time restrictions";
    return EvalStatus::kContinue;
  }

  auto policy_data = static_cast<UpdateCanBeAppliedPolicyData*>(data);

  // Set to true even if currently there are no restricted intervals. It may
  // change later and nothing else prevents download cancellation.
  policy_data->install_plan()->can_download_be_canceled = true;

  TimeProvider* const time_provider = state->time_provider();
  const Time* curr_date = ec->GetValue(time_provider->var_curr_date());
  const int* curr_hour = ec->GetValue(time_provider->var_curr_hour());
  const int* curr_minute = ec->GetValue(time_provider->var_curr_minute());
  if (!curr_date || !curr_hour || !curr_minute) {
    LOG(WARNING) << "Unable to access local time.";
    return EvalStatus::kContinue;
  }

  WeeklyTime now = WeeklyTime::FromTime(*curr_date);
  now.AddTime(base::Hours(*curr_hour) + base::Minutes(*curr_minute));

  const WeeklyTimeIntervalVector* intervals =
      ec->GetValue(dp_provider->var_disallowed_time_intervals());
  if (!intervals) {
    return EvalStatus::kContinue;
  }
  for (const auto& interval : *intervals) {
    if (interval.InRange(now)) {
      LOG(INFO) << "Deferring as time interval is within range.";
      policy_data->set_error_code(ErrorCode::kOmahaUpdateDeferredPerPolicy);
      return EvalStatus::kSucceeded;
    }
  }

  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
