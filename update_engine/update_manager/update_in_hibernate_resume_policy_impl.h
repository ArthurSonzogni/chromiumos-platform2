//
// Copyright (C) 2022 The Android Open Source Project
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
#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_IN_HIBERNATE_RESUME_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_IN_HIBERNATE_RESUME_POLICY_IMPL_H_

#include <string>

#include <base/time/time.h>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Define the amount of time that resume from hibernation can block an update
// from being applied. This value should be a balance between 1) the convenience
// of hibernate for the user in having all of their state nicely restored and 2)
// the importance of applying updates in a timely manner.
constexpr base::TimeDelta kMaxHibernateResumeTime = base::Hours(2);

// Policy that ensures updates are not applied when a resume from hibernation is
// in progress.
class UpdateInHibernateResumePolicyImpl : public PolicyInterface {
 public:
  UpdateInHibernateResumePolicyImpl() = default;
  UpdateInHibernateResumePolicyImpl(const UpdateInHibernateResumePolicyImpl&) =
      delete;
  UpdateInHibernateResumePolicyImpl& operator=(
      const UpdateInHibernateResumePolicyImpl&) = delete;

  ~UpdateInHibernateResumePolicyImpl() override = default;

  // Avoid applying an update when resuming from hibernation.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface*) const override;

 protected:
  std::string PolicyName() const override {
    return "UpdateInHibernateResumePolicyImpl";
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_IN_HIBERNATE_RESUME_POLICY_IMPL_H_
