// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Policy that allows administrators to set time intervals during which
// automatic update checks are disallowed. This implementation then checks if
// the current time falls in the range spanned by the time intervals. If the
// current time falls in one of the intervals then the update check is
// blocked by this policy.
class UpdateTimeRestrictionsPolicyImpl : public PolicyInterface {
 public:
  UpdateTimeRestrictionsPolicyImpl() = default;
  UpdateTimeRestrictionsPolicyImpl(const UpdateTimeRestrictionsPolicyImpl&) =
      delete;
  UpdateTimeRestrictionsPolicyImpl& operator=(
      const UpdateTimeRestrictionsPolicyImpl&) = delete;

  ~UpdateTimeRestrictionsPolicyImpl() override = default;

  // When the current time is inside one of the intervals returns
  // kSucceeded and sets |result| to kOmahaUpdateDeferredPerPolicy. If the
  // current time is not inside any intervals returns kContinue. In case of
  // errors, i.e. cannot access intervals or time, return kContinue.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface*) const override;

 protected:
  std::string PolicyName() const override {
    return "UpdateTimeRestrictionsPolicyImpl";
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_POLICY_IMPL_H_
