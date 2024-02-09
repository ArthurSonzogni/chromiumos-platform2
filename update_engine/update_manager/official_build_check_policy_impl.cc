// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/official_build_check_policy_impl.h"

#include <base/logging.h>

namespace chromeos_update_manager {

// Unofficial builds should not perform periodic update checks.
EvalStatus OnlyUpdateOfficialBuildsPolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    PolicyDataInterface* data) const {
  const bool* is_official_build_p =
      ec->GetValue(state->system_provider()->var_is_official_build());
  if (is_official_build_p != nullptr && !(*is_official_build_p)) {
    const int64_t* interval_timeout_p = ec->GetValue(
        state->updater_provider()->var_test_update_check_interval_timeout());
    // The |interval_timeout | is used for testing only to test periodic
    // update checks on unofficial images.
    if (interval_timeout_p == nullptr) {
      LOG(INFO) << "Unofficial build, blocking periodic update checks.";
      return EvalStatus::kAskMeAgainLater;
    }
    LOG(INFO) << "Unofficial build, but periodic update check interval "
              << "timeout is defined, so update is not blocked.";
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
