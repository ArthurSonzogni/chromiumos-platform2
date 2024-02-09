// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_

#include <string>

#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/state.h"

namespace chromeos_update_manager {

// The three different results of a policy request.
enum class EvalStatus {
  kFailed,
  kSucceeded,
  kAskMeAgainLater,
  kContinue,
};

class PolicyDataInterface {};

class PolicyInterface {
 public:
  virtual ~PolicyInterface() = default;

  virtual EvalStatus Evaluate(EvaluationContext* ec,
                              State* state,
                              std::string* error,
                              PolicyDataInterface* data) const = 0;
  virtual EvalStatus EvaluateDefault(EvaluationContext* ec,
                                     State* state,
                                     std::string* error,
                                     PolicyDataInterface* data) const {
    return EvalStatus::kSucceeded;
  }

  virtual std::string PolicyName() const = 0;

 protected:
  PolicyInterface() = default;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_POLICY_INTERFACE_H_
