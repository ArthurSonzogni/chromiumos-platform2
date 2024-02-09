// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/enough_slots_ab_updates_policy_impl.h"

#include "update_engine/update_manager/update_check_allowed_policy_data.h"

#include <base/logging.h>

namespace chromeos_update_manager {

// Do not perform any updates if booted from removable device. This decision
// is final.
EvalStatus EnoughSlotsAbUpdatesPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  UpdateCheckParams* result =
      UpdateCheckAllowedPolicyData::GetUpdateCheckParams(data);
  const auto* num_slots_p =
      ec->GetValue(state->system_provider()->var_num_slots());
  if (num_slots_p == nullptr || *num_slots_p < 2) {
    LOG(INFO) << "Not enough slots for A/B updates, disabling update checks.";
    result->updates_enabled = false;
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
