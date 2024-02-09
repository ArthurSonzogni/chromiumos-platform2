// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_RECOVERY_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_RECOVERY_POLICY_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Skip remaining policy checks if in MiniOs recovery.
class RecoveryPolicy : public PolicyInterface {
 public:
  RecoveryPolicy() = default;
  ~RecoveryPolicy() override = default;
  // Disallow copy and assign.
  RecoveryPolicy(const RecoveryPolicy&) = delete;
  RecoveryPolicy& operator=(const RecoveryPolicy&) = delete;

  // |PolicyInterface| overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "RecoveryPolicy"; }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_RECOVERY_POLICY_H_
