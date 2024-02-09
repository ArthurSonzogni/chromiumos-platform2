// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_DEVICE_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_DEVICE_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Check to see if Enterprise-managed (has DevicePolicy) and/or Kiosk-mode.  If
// so, then defer to those settings.
class EnterpriseDevicePolicyImpl : public PolicyInterface {
 public:
  EnterpriseDevicePolicyImpl() = default;
  EnterpriseDevicePolicyImpl(const EnterpriseDevicePolicyImpl&) = delete;
  EnterpriseDevicePolicyImpl& operator=(const EnterpriseDevicePolicyImpl&) =
      delete;

  ~EnterpriseDevicePolicyImpl() override = default;

  std::string PolicyName() const override {
    return "EnterpriseDevicePolicyImpl";
  }
  // Policy overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_ENTERPRISE_DEVICE_POLICY_IMPL_H_
