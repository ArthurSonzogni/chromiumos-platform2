// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/installation_policy_impl.h"

#include "update_engine/update_manager/update_check_allowed_policy_data.h"

#include <base/logging.h>

namespace chromeos_update_manager {

// If this is an installation, skip all subsequent policy checks.
EvalStatus InstallationPolicyImpl::Evaluate(EvaluationContext* ec,
                                            State* state,
                                            std::string* error,
                                            PolicyDataInterface* data) const {
  UpdaterProvider* const updater_provider = state->updater_provider();
  // Hack to force adding `var_forced_update_requested` into the evaluation
  // context value cache.
  (void)ec->GetValue(updater_provider->var_forced_update_requested());

  SystemProvider* const system_provider = state->system_provider();
  const bool* is_updating_p = ec->GetValue(system_provider->var_is_updating());
  if (is_updating_p && !(*is_updating_p)) {
    LOG(INFO) << "Installation, completing policy checks.";
    return EvalStatus::kSucceeded;
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
