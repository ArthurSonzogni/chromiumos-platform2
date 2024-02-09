// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
