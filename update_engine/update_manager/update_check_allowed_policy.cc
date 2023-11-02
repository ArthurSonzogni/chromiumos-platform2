//
// Copyright (C) 2014 The Android Open Source Project
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

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_util.h>

#include "update_engine/common/system_state.h"
#include "update_engine/update_manager/enough_slots_ab_updates_policy_impl.h"
#include "update_engine/update_manager/enterprise_device_policy_impl.h"
#include "update_engine/update_manager/installation_policy_impl.h"
#include "update_engine/update_manager/interactive_update_policy_impl.h"
#include "update_engine/update_manager/minimum_version_policy_impl.h"
#include "update_engine/update_manager/next_update_check_policy_impl.h"
#include "update_engine/update_manager/official_build_check_policy_impl.h"
#include "update_engine/update_manager/out_of_box_experience_policy_impl.h"
#include "update_engine/update_manager/policy_utils.h"
#include "update_engine/update_manager/recovery_policy.h"
#include "update_engine/update_manager/shill_provider.h"
#include "update_engine/update_manager/update_check_allowed_policy.h"
#include "update_engine/update_manager/update_in_hibernate_resume_policy_impl.h"

using chromeos_update_engine::SystemState;
using std::string;
using std::vector;

namespace chromeos_update_manager {

namespace {

// A fixed minimum interval between consecutive allowed update checks. This
// needs to be long enough to prevent busywork and/or DDoS attacks on Omaha, but
// at the same time short enough to allow the machine to update itself
// reasonably soon.
constexpr base::TimeDelta kCheckInterval = base::Minutes(15);

}  // namespace

EvalStatus UpdateCheckAllowedPolicy::Evaluate(EvaluationContext* ec,
                                              State* state,
                                              string* error,
                                              PolicyDataInterface* data) const {
  UpdateCheckParams* result =
      UpdateCheckAllowedPolicyData::GetUpdateCheckParams(data);
  // Set the default return values.
  result->updates_enabled = true;
  result->target_channel.clear();
  result->target_version_prefix.clear();
  result->rollback_on_channel_downgrade = false;
  result->interactive = false;

  InstallationPolicyImpl installation_policy;
  RecoveryPolicy recovery_policy;
  EnoughSlotsAbUpdatesPolicyImpl enough_slots_ab_updates_policy;
  EnterpriseDevicePolicyImpl enterprise_device_policy;
  OnlyUpdateOfficialBuildsPolicyImpl only_update_official_builds_policy;
  InteractiveUpdateCheckAllowedPolicyImpl interactive_update_policy;
  OobePolicyImpl oobe_policy;
  NextUpdateCheckTimePolicyImpl next_update_check_time_policy;
  UpdateInHibernateResumePolicyImpl hibernate_resume_policy;

  vector<PolicyInterface* const> policies_to_consult = {
      // Don't update when resuming from hibernate.
      &hibernate_resume_policy,

      // If this is an installation, allow performing.
      &installation_policy,

      // If in recovery mode, always check for update.
      &recovery_policy,

      // Do not perform any updates if there are not enough slots to do A/B
      // updates.
      &enough_slots_ab_updates_policy,

      // Check to see if Enterprise-managed (has DevicePolicy) and/or
      // Kiosk-mode.  If so, then defer to those settings.
      &enterprise_device_policy,

      // Check to see if an interactive update was requested.
      &interactive_update_policy,

      // Unofficial builds should not perform periodic update checks.
      &only_update_official_builds_policy,

      // If OOBE is enabled, wait until it is completed.
      &oobe_policy,

      // Ensure that periodic update checks are timed properly.
      &next_update_check_time_policy,
  };

  // Now that the list of policy implementations, and the order to consult them,
  // has been setup, consult the policies. If none of the policies make a
  // definitive decisions about whether or not to check for updates, then allow
  // the update check to happen.
  for (auto policy : policies_to_consult) {
    EvalStatus status = policy->Evaluate(ec, state, error, data);
    if (status != EvalStatus::kContinue) {
      return status;
    }
  }
  LOG(INFO) << "Allowing update check.";
  return EvalStatus::kSucceeded;
}

EvalStatus UpdateCheckAllowedPolicy::EvaluateDefault(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  UpdateCheckParams* result =
      UpdateCheckAllowedPolicyData::GetUpdateCheckParams(data);
  result->updates_enabled = true;
  result->target_channel.clear();
  result->target_version_prefix.clear();
  result->rollback_on_channel_downgrade = false;
  result->interactive = false;

  // Ensure that the minimum interval is set. If there's no clock, this defaults
  // to always allowing the update.
  if (!aux_state_->IsLastCheckAllowedTimeSet() ||
      ec->IsMonotonicTimeGreaterThan(aux_state_->last_check_allowed_time() +
                                     kCheckInterval)) {
    aux_state_->set_last_check_allowed_time(
        SystemState::Get()->clock()->GetMonotonicTime());
    return EvalStatus::kSucceeded;
  }

  return EvalStatus::kAskMeAgainLater;
}

}  // namespace chromeos_update_manager
