// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
