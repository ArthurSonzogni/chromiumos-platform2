//
// Copyright (C) 2014 The Android Open Source Project
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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_MANAGER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_MANAGER_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>

#include "update_engine/common/system_state.h"
#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/policy_evaluator.h"
#include "update_engine/update_manager/state.h"
#include "update_engine/update_manager/update_time_restrictions_monitor.h"

namespace chromeos_update_manager {

// The main Update Manager singleton class.
class UpdateManager {
 public:
  // Creates the UpdateManager instance, assuming ownership on the provided
  // |state|.
  UpdateManager(base::TimeDelta evaluation_timeout,
                base::TimeDelta expiration_timeout,
                State* state);
  UpdateManager(const UpdateManager&) = delete;
  UpdateManager& operator=(const UpdateManager&) = delete;

  virtual ~UpdateManager();

  // This function evaluates a given |policy| and returns the result
  // immediately. |data| is an input/output argument.  When the policy request
  // succeeds, the |data| is filled and the method returns
  // |EvalStatus::kSucceeded|, otherwise, the |data| may not be filled. A policy
  // called with this method should not block (i.e. return
  // |EvalStatus::kAskMeAgainLater|), which is considered a programming
  // error. On failure, |EvalStatus::kFailed| is returned.
  //
  // TODO(b/179419726): Remove "2" from the name once |PolicyRequest| functions
  // have been deprecated.
  EvalStatus PolicyRequest(std::unique_ptr<PolicyInterface> policy,
                           std::shared_ptr<PolicyDataInterface> data);

  // Similar to the function above but the results are returned at a later time
  // using the given |callback| function.
  // If the policy implementation should block, returning a
  // |EvalStatus::kAskMeAgainLater| status, the policy will be re-evaluated
  // until another status is returned. If the policy implementation based
  // its return value solely on const variables, the callback will be called
  // with the EvalStatus::kAskMeAgainLater status (which indicates an error).
  void PolicyRequest(std::unique_ptr<PolicyInterface> policy,
                     std::shared_ptr<PolicyDataInterface> data,
                     base::OnceCallback<void(EvalStatus)> callback);

  // Removes the |evaluator| from the internal list of |evaluators_|.
  void Unregister(PolicyEvaluator* evaluator);

  // Returns instance of update time restrictions monitor if |install_plan|
  // requires one. Otherwise returns nullptr.
  std::unique_ptr<UpdateTimeRestrictionsMonitor>
  BuildUpdateTimeRestrictionsMonitorIfNeeded(
      const chromeos_update_engine::InstallPlan& install_plan,
      UpdateTimeRestrictionsMonitor::Delegate* delegate);

 protected:
  // State getter used for testing.
  State* state() { return state_.get(); }

 private:
  FRIEND_TEST(UmUpdateManagerTest, PolicyRequestCallsPolicy);
  FRIEND_TEST(UmUpdateManagerTest, PolicyRequestCallsDefaultOnError);
  FRIEND_TEST(UmUpdateManagerTest, PolicyRequestDoesntBlockDeathTest);
  FRIEND_TEST(UmUpdateManagerTest, AsyncPolicyRequestDelaysEvaluation);
  FRIEND_TEST(UmUpdateManagerTest, AsyncPolicyRequestIsAddedToList);
  FRIEND_TEST(UmUpdateManagerTest, AsyncPolicyRequestTimeoutDoesNotFire);
  FRIEND_TEST(UmUpdateManagerTest, AsyncPolicyRequestTimesOut);

  // State Providers.
  std::unique_ptr<State> state_;

  // Timeout for a policy evaluation.
  const base::TimeDelta evaluation_timeout_;

  // Timeout for expiration of the evaluation context, used for async requests.
  const base::TimeDelta expiration_timeout_;

  std::vector<std::unique_ptr<PolicyEvaluator>> evaluators_;

  base::WeakPtrFactory<UpdateManager> weak_ptr_factory_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_MANAGER_H_
