// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
