//
// Copyright (C) 2023 The Android Open Source Project
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

#include "update_engine/update_manager/installation_policy_impl.h"

#include "update_engine/update_manager/update_check_allowed_policy_data.h"

#include <base/logging.h>

namespace chromeos_update_manager {

// If this is an installation, skip all subsequent policy checks.
EvalStatus InstallationPolicyImpl::Evaluate(EvaluationContext* ec,
                                            State* state,
                                            std::string* error,
                                            PolicyDataInterface* data) const {
  UpdaterProvider* const updater_provider = state->updater_provider();
  // Hack to force adding `var_forced_update_requested` into the evaluation
  // context value cache.
  (void)ec->GetValue(updater_provider->var_forced_update_requested());

  SystemProvider* const system_provider = state->system_provider();
  const bool* is_updating_p = ec->GetValue(system_provider->var_is_updating());
  if (is_updating_p && !(*is_updating_p)) {
    LOG(INFO) << "Installation, completing policy checks.";
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
