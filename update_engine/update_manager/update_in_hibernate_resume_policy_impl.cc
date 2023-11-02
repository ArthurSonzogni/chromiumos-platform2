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
#include "update_engine/update_manager/update_in_hibernate_resume_policy_impl.h"

#include <memory>

#include <base/logging.h>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/policy_utils.h"
#include "update_engine/update_manager/system_provider.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"

using base::Time;
using base::TimeDelta;
using std::string;

namespace chromeos_update_manager {

EvalStatus UpdateInHibernateResumePolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  SystemProvider* const system_provider = state->system_provider();
  const bool* is_resuming_p =
      ec->GetValue(system_provider->var_is_resuming_from_hibernate());
  if (is_resuming_p && *is_resuming_p) {
    // A system resuming from hibernate always boots that way, so use the
    // uptime of update_engine as a proxy for the boot time.
    UpdaterProvider* const updater_provider = state->updater_provider();
    const Time* updater_started_time =
        ec->GetValue(updater_provider->var_updater_started_time());
    POLICY_CHECK_VALUE_AND_FAIL(updater_started_time, error);

    // If the update has been deferred too long due to hibernate, abort the
    // resume from hibernation and continue.
    const TimeDelta delta = Time::Now() - *updater_started_time;
    if (delta > kMaxHibernateResumeTime) {
      LOG(INFO) << "Aborting resume from hibernate to apply an update.";
      const bool* is_aborted_p =
          ec->GetValue(system_provider->var_abort_resume_from_hibernate());

      // If the resume can't be aborted, it's still not a good idea to apply
      // updates. Defer in hope that the resume works itself out, or the
      // system reboots.
      if (!is_aborted_p || !*is_aborted_p) {
        LOG(ERROR) << "Failed to abort resume from hibernate.";
        return EvalStatus::kAskMeAgainLater;
      }

      return EvalStatus::kContinue;
    }

    LOG(INFO) << "Not updating while resuming from hibernate.";
    return EvalStatus::kAskMeAgainLater;
  }

  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
