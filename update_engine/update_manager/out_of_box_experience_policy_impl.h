// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_OUT_OF_BOX_EXPERIENCE_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_OUT_OF_BOX_EXPERIENCE_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// If OOBE is enabled, wait until it is completed.
class OobePolicyImpl : public PolicyInterface {
 public:
  OobePolicyImpl() = default;
  OobePolicyImpl(const OobePolicyImpl&) = delete;
  OobePolicyImpl& operator=(const OobePolicyImpl&) = delete;

  ~OobePolicyImpl() override = default;

  std::string PolicyName() const override { return "OobePolicyImpl"; }

  // Policy overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_OUT_OF_BOX_EXPERIENCE_POLICY_IMPL_H_
