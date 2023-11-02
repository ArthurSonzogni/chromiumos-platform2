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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_

#include <string>

#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/state.h"

namespace chromeos_update_manager {

// The three different results of a policy request.
enum class EvalStatus {
  kFailed,
  kSucceeded,
  kAskMeAgainLater,
  kContinue,
};

class PolicyDataInterface {};

class PolicyInterface {
 public:
  virtual ~PolicyInterface() = default;

  virtual EvalStatus Evaluate(EvaluationContext* ec,
                              State* state,
                              std::string* error,
                              PolicyDataInterface* data) const = 0;
  virtual EvalStatus EvaluateDefault(EvaluationContext* ec,
                                     State* state,
                                     std::string* error,
                                     PolicyDataInterface* data) const {
    return EvalStatus::kSucceeded;
  }

  virtual std::string PolicyName() const = 0;

 protected:
  PolicyInterface() = default;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_
