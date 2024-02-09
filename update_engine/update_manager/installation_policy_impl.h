// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_DLC_INSTALLATION_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_DLC_INSTALLATION_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Do not perform any updates if booted from removable device.
class InstallationPolicyImpl : public PolicyInterface {
 public:
  InstallationPolicyImpl() = default;
  InstallationPolicyImpl(const InstallationPolicyImpl&) = delete;
  InstallationPolicyImpl& operator=(const InstallationPolicyImpl&) = delete;

  ~InstallationPolicyImpl() override = default;

  // PolicyInterface overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "InstallationPolicyImpl"; }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_DLC_INSTALLATION_POLICY_IMPL_H_
