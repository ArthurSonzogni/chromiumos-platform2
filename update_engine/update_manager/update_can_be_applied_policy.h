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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_H_

#include <string>
#include <utility>

#include "update_engine/update_manager/policy_interface.h"
#include "update_engine/update_manager/update_check_allowed_policy_data.h"

namespace chromeos_update_manager {

class UpdateCanBeAppliedPolicy : public PolicyInterface {
 public:
  UpdateCanBeAppliedPolicy() = default;
  virtual ~UpdateCanBeAppliedPolicy() = default;

  UpdateCanBeAppliedPolicy(const UpdateCanBeAppliedPolicy&) = delete;
  UpdateCanBeAppliedPolicy& operator=(const UpdateCanBeAppliedPolicy&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

  EvalStatus EvaluateDefault(EvaluationContext* ec,
                             State* state,
                             std::string* error,
                             PolicyDataInterface* data) const override;

  std::string PolicyName() const override { return "UpdateCanBeAppliedPolicy"; }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_H_
