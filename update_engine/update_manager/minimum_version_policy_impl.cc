// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/minimum_version_policy_impl.h"

#include <base/logging.h>
#include <base/version.h>

#include "update_engine/update_manager/update_can_be_applied_policy_data.h"

using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::InstallPlan;
using std::string;

namespace chromeos_update_manager {

EvalStatus MinimumVersionPolicyImpl::Evaluate(EvaluationContext* ec,
                                              State* state,
                                              string* error,
                                              PolicyDataInterface* data) const {
  const base::Version* current_version(
      ec->GetValue(state->system_provider()->var_chromeos_version()));
  if (current_version == nullptr || !current_version->IsValid()) {
    LOG(WARNING) << "Unable to access current version";
    return EvalStatus::kContinue;
  }

  const base::Version* minimum_version = ec->GetValue(
      state->device_policy_provider()->var_device_minimum_version());
  if (minimum_version == nullptr || !minimum_version->IsValid()) {
    LOG(WARNING) << "Unable to access minimum version";
    return EvalStatus::kContinue;
  }

  if (*current_version < *minimum_version) {
    LOG(INFO) << "Updating from version less than minimum required"
                 ", allowing update to be applied.";
    static_cast<UpdateCanBeAppliedPolicyData*>(data)->set_error_code(
        ErrorCode::kSuccess);
    return EvalStatus::kSucceeded;
  }

  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
