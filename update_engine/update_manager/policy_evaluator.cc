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

#include "update_engine/update_manager/policy_evaluator.h"

#include <string>
#include <utility>

#include <base/logging.h>

namespace chromeos_update_manager {

PolicyEvaluator::~PolicyEvaluator() {
  Unregister();
}

void PolicyEvaluator::Unregister() {
  if (unregister_cb_)
    std::move(unregister_cb_).Run(this);
}

EvalStatus PolicyEvaluator::Evaluate() {
  // If expiration timeout fired, dump the context and reset expiration.
  // IMPORTANT: We must still proceed with evaluation of the policy in this
  // case, so that the evaluation time (and corresponding reevaluation
  // timeouts) are readjusted.
  if (ec_->is_expired()) {
    LOG(WARNING) << "Request timed out, evaluation context: "
                 << ec_->DumpContext();
    ec_->ResetExpiration();
  }

  // Reset the evaluation context.
  ec_->ResetEvaluation();

  // First try calling the actual policy.
  std::string error;
  EvalStatus status = policy_->Evaluate(ec_.get(), state_, &error, data_.get());
  // If evaluating the main policy failed, defer to the default policy.
  if (status == EvalStatus::kFailed) {
    LOG(WARNING) << "Evaluating policy failed: " << error
                 << "\nEvaluation context: " << ec_->DumpContext();
    error.clear();
    status = policy_->EvaluateDefault(ec_.get(), state_, &error, data_.get());
    if (status == EvalStatus::kFailed) {
      LOG(WARNING) << "Evaluating default policy failed: " << error;
    } else if (status == EvalStatus::kAskMeAgainLater) {
      LOG(ERROR)
          << "Default policy would block; this is a bug, forcing failure.";
      status = EvalStatus::kFailed;
    }
  }

  return status;
}

void PolicyEvaluator::ScheduleEvaluation(
    base::OnceCallback<void(EvalStatus)> callback) {
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::BindOnce(&PolicyEvaluator::OnPolicyReadyToEvaluate,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void PolicyEvaluator::OnPolicyReadyToEvaluate(
    base::OnceCallback<void(EvalStatus)> callback) {
  // Evaluate the policy.
  EvalStatus status = Evaluate();
  if (status != EvalStatus::kAskMeAgainLater) {
    std::move(callback).Run(status);
    Unregister();
    return;
  }

  // Re-schedule the policy request based on used variables.
  if (ec_->RunOnValueChangeOrTimeout(
          base::BindOnce(&PolicyEvaluator::OnPolicyReadyToEvaluate,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback))))
    return;  // Reevaluation scheduled successfully.

  // Scheduling a reevaluation can fail because policy method didn't use any
  // non-const variable nor there's any time-based event that will change the
  // status of evaluation. Alternatively, this may indicate an error in the
  // use of the scheduling interface.
  LOG(ERROR) << "Failed to schedule a reevaluation of policy"
             << "; this is a bug.";
  std::move(callback).Run(status);
  Unregister();
}

}  // namespace chromeos_update_manager
