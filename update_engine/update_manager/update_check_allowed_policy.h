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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_H_

#include <memory>
#include <string>
#include <utility>

#include "update_engine/update_manager/policy_interface.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"

namespace chromeos_update_manager {

class UpdateCheckAllowedPolicy : public PolicyInterface {
 public:
  // Auxiliary state class for DefaultPolicy evaluations.
  //
  // IMPORTANT: The use of a state object in policies is generally forbidden, as
  // it was a design decision to keep policy calls side-effect free. We make an
  // exception here to ensure that DefaultPolicy indeed serves as a safe (and
  // secure) fallback option. This practice should be avoided when imlpementing
  // other policies.
  class DefaultPolicyState {
   public:
    DefaultPolicyState() {}

    bool IsLastCheckAllowedTimeSet() const {
      return last_check_allowed_time_ != base::Time::Max();
    }

    // Sets/returns the point time on the monotonic time scale when the latest
    // check allowed was recorded.
    void set_last_check_allowed_time(base::Time timestamp) {
      last_check_allowed_time_ = timestamp;
    }
    base::Time last_check_allowed_time() const {
      return last_check_allowed_time_;
    }

   private:
    base::Time last_check_allowed_time_ = base::Time::Max();
  };

  UpdateCheckAllowedPolicy() : aux_state_(new DefaultPolicyState()) {}
  virtual ~UpdateCheckAllowedPolicy() = default;

  UpdateCheckAllowedPolicy(const UpdateCheckAllowedPolicy&) = delete;
  UpdateCheckAllowedPolicy& operator=(const UpdateCheckAllowedPolicy&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

  EvalStatus EvaluateDefault(EvaluationContext* ec,
                             State* state,
                             std::string* error,
                             PolicyDataInterface* data) const override;

  std::string PolicyName() const override { return "UpdateCheckAllowedPolicy"; }

 private:
  // An auxiliary state object.
  std::unique_ptr<DefaultPolicyState> aux_state_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_H_
