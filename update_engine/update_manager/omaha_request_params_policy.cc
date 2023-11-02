//
// Copyright 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/update_manager/omaha_request_params_policy.h"

#include <string>

#include <base/logging.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/system_state.h"
#include "update_engine/cros/omaha_request_params.h"

using chromeos_update_engine::SystemState;
using std::string;

namespace chromeos_update_manager {

namespace {
constexpr char kMarketSegmentConsumer[] = "consumer";
}  // namespace

EvalStatus OmahaRequestParamsPolicy::Evaluate(EvaluationContext* ec,
                                              State* state,
                                              string* error,
                                              PolicyDataInterface* data) const {
  auto request_params = SystemState::Get()->request_params();

  const bool* market_segment_disabled_p =
      ec->GetValue(state->updater_provider()->var_market_segment_disabled());
  if (market_segment_disabled_p == nullptr || !(*market_segment_disabled_p)) {
    request_params->set_market_segment(kMarketSegmentConsumer);
  }

  // If no device policy was loaded, nothing else to do.
  DevicePolicyProvider* const dp_provider = state->device_policy_provider();
  const bool* device_policy_is_loaded_p =
      ec->GetValue(dp_provider->var_device_policy_is_loaded());
  if (!device_policy_is_loaded_p || !(*device_policy_is_loaded_p)) {
    return EvalStatus::kContinue;
  }

  if (market_segment_disabled_p == nullptr || !(*market_segment_disabled_p)) {
    const string* market_segment_p =
        ec->GetValue(dp_provider->var_market_segment());
    if (market_segment_p) {
      request_params->set_market_segment(*market_segment_p);
    }
  }

  const string* quick_fix_build_token_p =
      ec->GetValue(dp_provider->var_quick_fix_build_token());
  if (quick_fix_build_token_p) {
    request_params->set_quick_fix_build_token(*quick_fix_build_token_p);
  }

  const string* release_lts_tag_p =
      ec->GetValue(dp_provider->var_release_lts_tag());
  if (release_lts_tag_p) {
    request_params->set_release_lts_tag(*release_lts_tag_p);
  }

  // Policy always overwrites whether rollback is allowed by the kiosk app
  // manifest.
  const RollbackToTargetVersion* rollback_to_target_version_p =
      ec->GetValue(dp_provider->var_rollback_to_target_version());
  // Set the default values, just in case.
  request_params->set_rollback_allowed(false);
  request_params->set_rollback_data_save_requested(false);
  if (rollback_to_target_version_p) {
    switch (*rollback_to_target_version_p) {
      case RollbackToTargetVersion::kUnspecified:
        break;
      case RollbackToTargetVersion::kDisabled:
        LOG(INFO) << "Policy disables rollbacks.";
        break;
      case RollbackToTargetVersion::kRollbackAndPowerwash:
        LOG(INFO) << "Policy allows rollbacks with powerwash.";
        request_params->set_rollback_allowed(true);
        break;
      case RollbackToTargetVersion::kRollbackAndRestoreIfPossible:
        LOG(INFO)
            << "Policy allows rollbacks, also tries to restore if possible.";
        request_params->set_rollback_allowed(true);
        request_params->set_rollback_data_save_requested(true);
        break;
      case RollbackToTargetVersion::kMaxValue:
        NOTREACHED();
        // Don't add a default case to let the compiler warn about newly
        // added enum values which should be added here.
    }
  }

  // Determine allowed milestones for rollback
  const int* rollback_allowed_milestones_p =
      ec->GetValue(dp_provider->var_rollback_allowed_milestones());
  if (rollback_allowed_milestones_p)
    request_params->set_rollback_allowed_milestones(
        *rollback_allowed_milestones_p);

  return EvalStatus::kSucceeded;
}

}  // namespace chromeos_update_manager
