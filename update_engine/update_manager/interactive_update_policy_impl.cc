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

#include "update_engine/update_manager/interactive_update_policy_impl.h"

#include <base/logging.h>

#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"

using chromeos_update_engine::ErrorCode;

namespace chromeos_update_manager {

// Check to see if an interactive update was requested.
EvalStatus InteractiveUpdateCheckAllowedPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  bool interactive;
  if (CheckInteractiveUpdateRequested(
          ec, state->updater_provider(), &interactive)) {
    UpdateCheckParams* result =
        UpdateCheckAllowedPolicyData::GetUpdateCheckParams(data);
    result->interactive = interactive;
    LOG(INFO) << "Forced update signaled ("
              << (interactive ? "interactive" : "periodic")
              << "), allowing update check.";
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

// TODO(b/179419726): Move after the next function to keep order better.
EvalStatus InteractiveUpdateCanBeAppliedPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  UpdateCheckAllowedPolicyData uca_data;
  InteractiveUpdateCheckAllowedPolicyImpl uca_policy;
  if (uca_policy.Evaluate(ec, state, error, &uca_data) ==
      EvalStatus::kSucceeded) {
    static_cast<UpdateCanBeAppliedPolicyData*>(data)->set_error_code(
        ErrorCode::kSuccess);
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

bool InteractiveUpdateCheckAllowedPolicyImpl::CheckInteractiveUpdateRequested(
    EvaluationContext* ec,
    UpdaterProvider* const updater_provider,
    bool* interactive_out) const {
  // First, check to see if an interactive update was requested.
  const UpdateRequestStatus* forced_update_requested_p =
      ec->GetValue(updater_provider->var_forced_update_requested());
  if (forced_update_requested_p != nullptr &&
      *forced_update_requested_p != UpdateRequestStatus::kNone) {
    if (interactive_out)
      *interactive_out =
          (*forced_update_requested_p == UpdateRequestStatus::kInteractive);
    return true;
  }
  return false;
}

}  // namespace chromeos_update_manager
