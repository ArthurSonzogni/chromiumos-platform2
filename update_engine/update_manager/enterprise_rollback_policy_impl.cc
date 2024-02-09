// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/enterprise_rollback_policy_impl.h"

#include "update_engine/update_manager/update_can_be_applied_policy_data.h"

#include <base/logging.h>

using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::InstallPlan;
using std::string;

namespace chromeos_update_manager {

EvalStatus EnterpriseRollbackPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  auto policy_data = static_cast<UpdateCanBeAppliedPolicyData*>(data);
  if (policy_data->install_plan()->is_rollback) {
    LOG(INFO)
        << "Update is enterprise rollback, allowing update to be applied.";
    policy_data->set_error_code(ErrorCode::kSuccess);
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
