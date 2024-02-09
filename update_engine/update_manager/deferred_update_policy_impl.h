// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_DEFERRED_UPDATE_POLICY_IMPL_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_DEFERRED_UPDATE_POLICY_IMPL_H_

#include <string>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

class DeferredUpdatePolicyImpl : public PolicyInterface {
 public:
  DeferredUpdatePolicyImpl() = default;
  DeferredUpdatePolicyImpl(const DeferredUpdatePolicyImpl&) = delete;
  DeferredUpdatePolicyImpl& operator=(const DeferredUpdatePolicyImpl&) = delete;

  ~DeferredUpdatePolicyImpl() override = default;

  std::string PolicyName() const override { return "DeferredUpdatePolicyImpl"; }

  // Policy overrides.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_DEFERRED_UPDATE_POLICY_IMPL_H_
