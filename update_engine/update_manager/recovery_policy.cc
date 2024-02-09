// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/recovery_policy.h"

#include <base/logging.h>

#include "update_engine/update_manager/interactive_update_policy_impl.h"

namespace chromeos_update_manager {

EvalStatus RecoveryPolicy::Evaluate(EvaluationContext* ec,
                                    State* state,
                                    std::string* error,
                                    PolicyDataInterface* data) const {
  const bool* running_in_minios =
      ec->GetValue(state->config_provider()->var_is_running_from_minios());
  if (running_in_minios && (*running_in_minios)) {
    InteractiveUpdateCanBeAppliedPolicyImpl interactive_update_policy;
    EvalStatus status =
        interactive_update_policy.Evaluate(ec, state, error, data);
    if (status != EvalStatus::kContinue) {
      LOG(INFO) << "In Recovery Mode, allowing interactive update checks.";
      return status;
    }
    LOG(INFO) << "In Recovery Mode, ignoring non-interactive update checks.";
    return EvalStatus::kAskMeAgainLater;
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
