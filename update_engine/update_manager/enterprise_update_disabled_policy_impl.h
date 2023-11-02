//
// Copyright 2023 The Android Open Source Project
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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_UPDATE_DISABLED_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_UPDATE_DISABLED_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Checks if updates are disabled by the enterprise policy.
class EnterpriseUpdateDisabledPolicyImpl : public PolicyInterface {
 public:
  EnterpriseUpdateDisabledPolicyImpl() = default;
  virtual ~EnterpriseUpdateDisabledPolicyImpl() = default;

  EnterpriseUpdateDisabledPolicyImpl(
      const EnterpriseUpdateDisabledPolicyImpl&) = delete;
  EnterpriseUpdateDisabledPolicyImpl& operator=(
      const EnterpriseUpdateDisabledPolicyImpl&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

  std::string PolicyName() const override {
    return "EnterpriseUpdateDisabledPolicyImpl";
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_UPDATE_DISABLED_POLICY_IMPL_H_
