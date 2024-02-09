// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_OFFICIAL_BUILD_CHECK_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_OFFICIAL_BUILD_CHECK_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Unofficial builds should not perform periodic update checks.
class OnlyUpdateOfficialBuildsPolicyImpl : public PolicyInterface {
 public:
  OnlyUpdateOfficialBuildsPolicyImpl() = default;
  OnlyUpdateOfficialBuildsPolicyImpl(
      const OnlyUpdateOfficialBuildsPolicyImpl&) = delete;
  OnlyUpdateOfficialBuildsPolicyImpl& operator=(
      const OnlyUpdateOfficialBuildsPolicyImpl&) = delete;

  ~OnlyUpdateOfficialBuildsPolicyImpl() override = default;

  // Policy overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override {
    return "OnlyUpdateOfficialBuildsPolicyImpl";
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_OFFICIAL_BUILD_CHECK_POLICY_IMPL_H_
