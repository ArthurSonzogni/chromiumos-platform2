//
// Copyright 2021 The Android Open Source Project
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

#include "update_engine/update_manager/p2p_enabled_policy.h"

#include <string>

using std::string;

namespace chromeos_update_manager {

const int kMaxP2PAttempts = 10;
const base::TimeDelta kMaxP2PAttemptsPeriod = base::Days(5);

EvalStatus P2PEnabledPolicy::Evaluate(EvaluationContext* ec,
                                      State* state,
                                      string* error,
                                      PolicyDataInterface* data) const {
  bool enabled = false;

  // Determine whether use of P2P is allowed by policy. Even if P2P is not
  // explicitly allowed, we allow it if the device is enterprise enrolled (that
  // is, missing or empty owner string).
  DevicePolicyProvider* const dp_provider = state->device_policy_provider();
  const bool* device_policy_is_loaded_p =
      ec->GetValue(dp_provider->var_device_policy_is_loaded());
  if (device_policy_is_loaded_p && *device_policy_is_loaded_p) {
    const bool* policy_au_p2p_enabled_p =
        ec->GetValue(dp_provider->var_au_p2p_enabled());
    if (policy_au_p2p_enabled_p) {
      enabled = *policy_au_p2p_enabled_p;
    }
  }

  // Enable P2P, if so mandated by the updater configuration. This is additive
  // to whether or not P2P is enabled by device policy.
  if (!enabled) {
    const bool* updater_p2p_enabled_p =
        ec->GetValue(state->updater_provider()->var_p2p_enabled());
    enabled = updater_p2p_enabled_p && *updater_p2p_enabled_p;
  }

  static_cast<P2PEnabledPolicyData*>(data)->set_enabled(enabled);
  return EvalStatus::kSucceeded;
}

EvalStatus P2PEnabledPolicy::EvaluateDefault(EvaluationContext* ec,
                                             State* state,
                                             std::string* error,
                                             PolicyDataInterface* data) const {
  static_cast<P2PEnabledPolicyData*>(data)->set_enabled(false);
  return EvalStatus::kSucceeded;
}

EvalStatus P2PEnabledChangedPolicy::Evaluate(EvaluationContext* ec,
                                             State* state,
                                             string* error,
                                             PolicyDataInterface* data) const {
  P2PEnabledPolicy policy;
  EvalStatus status = policy.Evaluate(ec, state, error, data);
  auto p2p_data = static_cast<P2PEnabledPolicyData*>(data);
  if (status == EvalStatus::kSucceeded &&
      p2p_data->enabled() == p2p_data->prev_enabled())
    return EvalStatus::kAskMeAgainLater;
  return status;
}

EvalStatus P2PEnabledChangedPolicy::EvaluateDefault(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  // This policy will always prohibit P2P, so this is signaling to the caller
  // that the decision is final (because the current value is the same as the
  // previous one) and there's no need to issue another call.
  static_cast<P2PEnabledPolicyData*>(data)->set_enabled(false);
  return EvalStatus::kSucceeded;
}

}  // namespace chromeos_update_manager
