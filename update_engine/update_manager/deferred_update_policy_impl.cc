// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/deferred_update_policy_impl.h"

#include "update_engine/common/constants.h"
#include "update_engine/common/system_state.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"

#include <base/logging.h>

using chromeos_update_engine::DeferUpdateAction;

namespace chromeos_update_manager {

// Defer updates if consumer has disabled auto updates.
EvalStatus DeferredUpdatePolicyImpl::Evaluate(EvaluationContext* ec,
                                              State* state,
                                              std::string* error,
                                              PolicyDataInterface* data) const {
  DevicePolicyProvider* const dp_provider = state->device_policy_provider();

  auto* policy_data = static_cast<UpdateCanBeAppliedPolicyData*>(data);
  auto* install_plan = policy_data->install_plan();

  // Althought the default for `defer_update_action` is `kOff`, explicitly set
  // in order to not cause potential accidental overrides/defaults missing.
  install_plan->defer_update_action = DeferUpdateAction::kOff;

  // Skip check if device is managed.
  const bool* has_owner_p = ec->GetValue(dp_provider->var_has_owner());
  if (has_owner_p && !(*has_owner_p)) {
    LOG(INFO) << "Managed device, not deferring updates.";
    return EvalStatus::kContinue;
  }

  // Otherwise, check if the consumer device has auto updates disabled.
  const bool* updater_consumer_auto_update_disabled_p = ec->GetValue(
      state->updater_provider()->var_consumer_auto_update_disabled());
  if (updater_consumer_auto_update_disabled_p) {
    // Consumer auto update is enabled.
    if (!(*updater_consumer_auto_update_disabled_p)) {
      LOG(INFO) << "Consumer auto update is enabled, not deferring updates.";
      return EvalStatus::kContinue;
    }

    // Consumer auto update is disabled.
    LOG(INFO) << "Consumer auto update is disabled, deferring updates.";
    install_plan->defer_update_action = DeferUpdateAction::kHold;
    // The installer (postinstall) script will hold back the partition table
    // update, so we must do the same from the autoupdater side.
    install_plan->switch_slot_on_reboot = false;
    return EvalStatus::kContinue;
  }

  LOG(WARNING)
      << "Couldn't find consumer auto update value, not deferring updates.";
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
