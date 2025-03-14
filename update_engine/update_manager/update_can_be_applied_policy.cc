// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/update_can_be_applied_policy.h"

#include <string>
#include <vector>

#include <base/logging.h>

#include "update_engine/common/error_code.h"
#include "update_engine/update_manager/deferred_update_policy_impl.h"
#include "update_engine/update_manager/enterprise_rollback_policy_impl.h"
#include "update_engine/update_manager/interactive_update_policy_impl.h"
#include "update_engine/update_manager/minimum_version_policy_impl.h"
#include "update_engine/update_manager/update_can_be_applied_policy_data.h"
#include "update_engine/update_manager/update_time_restrictions_policy_impl.h"

using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::InstallPlan;
using std::string;
using std::vector;

namespace chromeos_update_manager {

EvalStatus UpdateCanBeAppliedPolicy::Evaluate(EvaluationContext* ec,
                                              State* state,
                                              string* error,
                                              PolicyDataInterface* data) const {
  InteractiveUpdateCanBeAppliedPolicyImpl interactive_update_policy;
  EnterpriseRollbackPolicyImpl enterprise_rollback_policy;
  MinimumVersionPolicyImpl minimum_version_policy;
  UpdateTimeRestrictionsPolicyImpl update_time_restrictions_policy;
  DeferredUpdatePolicyImpl deferred_update_policy;

  vector<PolicyInterface*> policies_to_consult = {
      // Check to see if an interactive update has been requested.
      &interactive_update_policy,

      // Check whether current update is enterprise rollback.
      &enterprise_rollback_policy,

      // Check whether update happens from a version less than the minimum
      // required one.
      &minimum_version_policy,

      // Do not apply or download an update if we are inside one of the
      // restricted times.
      &update_time_restrictions_policy,

      // Check to see if deferred updates is required.
      // Note: Always run later than interactive policy check.
      &deferred_update_policy,
  };

  for (auto policy : policies_to_consult) {
    EvalStatus status = policy->Evaluate(ec, state, error, data);
    if (status != EvalStatus::kContinue) {
      return status;
    }
  }
  LOG(INFO) << "Allowing update to be applied.";
  static_cast<UpdateCanBeAppliedPolicyData*>(data)->set_error_code(
      ErrorCode::kSuccess);
  return EvalStatus::kSucceeded;
}

EvalStatus UpdateCanBeAppliedPolicy::EvaluateDefault(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  static_cast<UpdateCanBeAppliedPolicyData*>(data)->set_error_code(
      ErrorCode::kSuccess);
  return EvalStatus::kSucceeded;
}

}  // namespace chromeos_update_manager
