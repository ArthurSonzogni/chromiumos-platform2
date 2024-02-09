// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_POLICY_EVALUATOR_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_POLICY_EVALUATOR_H_

#include <memory>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>

#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/policy_interface.h"
#include "update_engine/update_manager/state.h"

namespace chromeos_update_manager {

// This class is the main point of entry for evaluating any kind of policy.
class PolicyEvaluator {
 public:
  using UnregisterCallback = base::OnceCallback<void(PolicyEvaluator*)>;

  PolicyEvaluator(State* state,
                  std::unique_ptr<EvaluationContext> ec,
                  std::unique_ptr<PolicyInterface> policy,
                  std::shared_ptr<PolicyDataInterface> data,
                  UnregisterCallback unregister_cb = {})
      : state_(state),
        ec_(std::move(ec)),
        policy_(std::move(policy)),
        data_(std::move(data)),
        unregister_cb_(std::move(unregister_cb)),
        weak_ptr_factory_(this) {}
  virtual ~PolicyEvaluator();

  PolicyEvaluator(const PolicyEvaluator&) = delete;
  PolicyEvaluator& operator=(const PolicyEvaluator&) = delete;

  // Unregisters the current object from its owner. This object will probably
  // gets deleted after calling this function, so there should be no member
  // access after this function has been called.
  void Unregister();

  // Evaluations the policy given in the ctor using the provided |data_| and
  // returns the result of the evaluation.
  EvalStatus Evaluate();

  // Same as the above function but the asyncronous version. A call to this
  // function returns immediately and an evalution is scheduled in the main
  // message loop. The passed |callback| is called when the policy is evaluated.
  void ScheduleEvaluation(base::OnceCallback<void(EvalStatus)> callback);

 private:
  // Internal function to reschedule policy evaluation.
  void OnPolicyReadyToEvaluate(base::OnceCallback<void(EvalStatus)> callback);
  State* state_;
  std::unique_ptr<EvaluationContext> ec_;
  std::unique_ptr<PolicyInterface> policy_;
  std::shared_ptr<PolicyDataInterface> data_;

  UnregisterCallback unregister_cb_;

  base::WeakPtrFactory<PolicyEvaluator> weak_ptr_factory_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_POLICY_EVALUATOR_H_
