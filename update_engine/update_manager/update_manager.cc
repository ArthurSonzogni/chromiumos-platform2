// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/update_manager.h"

#include <utility>

#include "update_engine/update_manager/state.h"

namespace chromeos_update_manager {

UpdateManager::UpdateManager(base::TimeDelta evaluation_timeout,
                             base::TimeDelta expiration_timeout,
                             State* state)
    : state_(state),
      evaluation_timeout_(evaluation_timeout),
      expiration_timeout_(expiration_timeout),
      weak_ptr_factory_(this) {}

UpdateManager::~UpdateManager() = default;

EvalStatus UpdateManager::PolicyRequest(
    std::unique_ptr<PolicyInterface> policy,
    std::shared_ptr<PolicyDataInterface> data) {
  return PolicyEvaluator(
             state_.get(),
             std::make_unique<EvaluationContext>(evaluation_timeout_),
             std::move(policy), std::move(data))
      .Evaluate();
}

void UpdateManager::PolicyRequest(
    std::unique_ptr<PolicyInterface> policy,
    std::shared_ptr<PolicyDataInterface> data,
    base::OnceCallback<void(EvalStatus)> callback) {
  auto ec = std::make_unique<EvaluationContext>(evaluation_timeout_,
                                                expiration_timeout_);
  evaluators_.push_back(std::make_unique<PolicyEvaluator>(
      state_.get(), std::move(ec), std::move(policy), std::move(data),
      base::BindOnce(&UpdateManager::Unregister,
                     weak_ptr_factory_.GetWeakPtr())));
  evaluators_.back()->ScheduleEvaluation(std::move(callback));
}

void UpdateManager::Unregister(PolicyEvaluator* evaluator) {
  // TODO(ahassani): Once we move to C++20 use |std::erase_if()| instead.
  auto it =
      std::find_if(evaluators_.begin(), evaluators_.end(),
                   [evaluator](const std::unique_ptr<PolicyEvaluator>& e) {
                     return e.get() == evaluator;
                   });
  if (it != evaluators_.end()) {
    evaluators_.erase(it);
  }
}

std::unique_ptr<UpdateTimeRestrictionsMonitor>
UpdateManager::BuildUpdateTimeRestrictionsMonitorIfNeeded(
    const chromeos_update_engine::InstallPlan& install_plan,
    UpdateTimeRestrictionsMonitor::Delegate* delegate) {
  if (!install_plan.can_download_be_canceled || delegate == nullptr)
    return nullptr;

  return std::make_unique<UpdateTimeRestrictionsMonitor>(
      state_->device_policy_provider(), delegate);
}

}  // namespace chromeos_update_manager
