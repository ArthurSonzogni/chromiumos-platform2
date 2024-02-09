// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/out_of_box_experience_policy_impl.h"

#include <base/logging.h>

#include "update_engine/common/utils.h"

namespace chromeos_update_manager {

EvalStatus OobePolicyImpl::Evaluate(EvaluationContext* ec,
                                    State* state,
                                    std::string* error,
                                    PolicyDataInterface* data) const {
  SystemProvider* const system_provider = state->system_provider();

  // If OOBE is enabled, wait until it is completed.
  // Unless the request is for non-updates.
  const bool* is_updating_p =
      ec->GetValue(state->system_provider()->var_is_updating());
  if (is_updating_p && !(*is_updating_p)) {
    LOG(INFO) << "Skipping policy for non-updates.";
    return EvalStatus::kContinue;
  }

  const bool* is_oobe_enabled_p =
      ec->GetValue(state->config_provider()->var_is_oobe_enabled());
  if (is_oobe_enabled_p && *is_oobe_enabled_p) {
    const bool* is_oobe_complete_p =
        ec->GetValue(system_provider->var_is_oobe_complete());
    if (is_oobe_complete_p && !(*is_oobe_complete_p)) {
      LOG(INFO) << "OOBE not completed, blocking update checks.";
      return EvalStatus::kAskMeAgainLater;
    }
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
