// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_ROLLBACK_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_ROLLBACK_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// If the update is an enterprise rollback, this should not block the update
// to be applied.
class EnterpriseRollbackPolicyImpl : public PolicyInterface {
 public:
  EnterpriseRollbackPolicyImpl() = default;
  ~EnterpriseRollbackPolicyImpl() override = default;

  // Policy overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override {
    return "EnterpriseRollbackPolicyImpl";
  }

 private:
  EnterpriseRollbackPolicyImpl(const EnterpriseRollbackPolicyImpl&) = delete;
  EnterpriseRollbackPolicyImpl& operator=(const EnterpriseRollbackPolicyImpl&) =
      delete;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_ROLLBACK_POLICY_IMPL_H_
