// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_ENOUGH_SLOTS_AB_UPDATES_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_ENOUGH_SLOTS_AB_UPDATES_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Do not perform any updates if booted from removable device.
class EnoughSlotsAbUpdatesPolicyImpl : public PolicyInterface {
 public:
  EnoughSlotsAbUpdatesPolicyImpl() = default;
  EnoughSlotsAbUpdatesPolicyImpl(const EnoughSlotsAbUpdatesPolicyImpl&) =
      delete;
  EnoughSlotsAbUpdatesPolicyImpl& operator=(
      const EnoughSlotsAbUpdatesPolicyImpl&) = delete;

  ~EnoughSlotsAbUpdatesPolicyImpl() override = default;

  // PolicyInterface overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override {
    return "EnoughSlotsAbUpdatesPolicyImpl";
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_ENOUGH_SLOTS_AB_UPDATES_POLICY_IMPL_H_
