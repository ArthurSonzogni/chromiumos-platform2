// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_MINIMUM_VERSION_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_MINIMUM_VERSION_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Check to see if an update happens from a version less than the minimum
// required one.
class MinimumVersionPolicyImpl : public PolicyInterface {
 public:
  MinimumVersionPolicyImpl() = default;
  ~MinimumVersionPolicyImpl() override = default;

  // If current version is less than the minimum required one, then this should
  // not block the update to be applied.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "MinimumVersionPolicyImpl"; }

 private:
  MinimumVersionPolicyImpl(const MinimumVersionPolicyImpl&) = delete;
  MinimumVersionPolicyImpl& operator=(const MinimumVersionPolicyImpl&) = delete;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_MINIMUM_VERSION_POLICY_IMPL_H_
