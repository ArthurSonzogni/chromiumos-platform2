// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_OMAHA_REQUEST_PARAMS_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_OMAHA_REQUEST_PARAMS_POLICY_H_

#include <string>
#include <utility>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

class OmahaRequestParamsPolicy : public PolicyInterface {
 public:
  OmahaRequestParamsPolicy() = default;
  virtual ~OmahaRequestParamsPolicy() = default;

  OmahaRequestParamsPolicy(const OmahaRequestParamsPolicy&) = delete;
  OmahaRequestParamsPolicy& operator=(const OmahaRequestParamsPolicy&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "OmahaRequestParamsPolicy"; }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_OMAHA_REQUEST_PARAMS_POLICY_H_
