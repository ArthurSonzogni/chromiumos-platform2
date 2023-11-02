//
// Copyright 2023 The Android Open Source Project
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

#include "update_engine/update_manager/enterprise_update_disabled_policy_impl.h"

#include <string>

#include "update_engine/update_manager/device_policy_provider.h"

using std::string;

namespace chromeos_update_manager {

EvalStatus EnterpriseUpdateDisabledPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  DevicePolicyProvider* const device_policy_provider =
      state->device_policy_provider();

  const bool* is_enterprise_enrolled =
      ec->GetValue(device_policy_provider->var_is_enterprise_enrolled());
  if (!(is_enterprise_enrolled && *is_enterprise_enrolled)) {
    return EvalStatus::kContinue;
  }

  const bool* update_disabled =
      ec->GetValue(device_policy_provider->var_update_disabled());
  if (!(update_disabled && *update_disabled)) {
    return EvalStatus::kAskMeAgainLater;
  }

  return EvalStatus::kSucceeded;
}

}  // namespace chromeos_update_manager
