//
// Copyright (C) 2017 The Android Open Source Project
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

#include "update_engine/update_manager/enterprise_device_policy_impl.h"

#include <base/logging.h>
#include <base/notreached.h>

#include "update_engine/common/utils.h"
#include "update_engine/cros/update_attempter.h"

#include "update_engine/update_manager/update_check_allowed_policy_data.h"

using std::string;

namespace chromeos_update_manager {

// Check to see if Enterprise-managed (has DevicePolicy) and/or Kiosk-mode.  If
// so, then defer to those settings.
EvalStatus EnterpriseDevicePolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  DevicePolicyProvider* const dp_provider = state->device_policy_provider();
  SystemProvider* const system_provider = state->system_provider();
  UpdateCheckParams* result =
      UpdateCheckAllowedPolicyData::GetUpdateCheckParams(data);

  const bool* device_policy_is_loaded_p =
      ec->GetValue(dp_provider->var_device_policy_is_loaded());
  if (device_policy_is_loaded_p && *device_policy_is_loaded_p) {
    bool kiosk_app_control_chrome_version = false;

    // Check whether updates are disabled by policy.
    // Only care if processing updates, installations should flow through.
    const bool* is_updating_p =
        ec->GetValue(system_provider->var_is_updating());
    if (is_updating_p && !(*is_updating_p)) {
      LOG(INFO) << "Skipping policy for non-updates.";
      return EvalStatus::kContinue;
    }

    const bool* update_disabled_p =
        ec->GetValue(dp_provider->var_update_disabled());
    if (update_disabled_p && *update_disabled_p) {
      // Check whether allow kiosk app to control chrome version policy. This
      // policy is only effective when AU is disabled by admin.
      const bool* allow_kiosk_app_control_chrome_version_p = ec->GetValue(
          dp_provider->var_allow_kiosk_app_control_chrome_version());
      kiosk_app_control_chrome_version =
          allow_kiosk_app_control_chrome_version_p &&
          *allow_kiosk_app_control_chrome_version_p;
      if (!kiosk_app_control_chrome_version) {
        // No kiosk pin chrome version policy. AU is really disabled.
        LOG(INFO) << "Updates disabled by policy, blocking update checks.";
        return EvalStatus::kAskMeAgainLater;
      }
    }

    // By default, result->rollback_allowed is false.
    if (kiosk_app_control_chrome_version) {
      // Get the required platform version from Chrome.
      const string* kiosk_required_platform_version_p =
          ec->GetValue(system_provider->var_kiosk_required_platform_version());
      if (!kiosk_required_platform_version_p) {
        LOG(INFO) << "Kiosk app required platform version is not fetched, "
                     "blocking update checks.";
        return EvalStatus::kAskMeAgainLater;
      } else if (kiosk_required_platform_version_p->empty()) {
        // The platform version could not be fetched several times. Update
        // based on |DeviceMinimumVersion| instead (crbug.com/1048931).
        const base::Version* device_minimum_version_p =
            ec->GetValue(dp_provider->var_device_minimum_version());
        const base::Version* current_version_p(
            ec->GetValue(system_provider->var_chromeos_version()));
        if (device_minimum_version_p && device_minimum_version_p->IsValid() &&
            current_version_p && current_version_p->IsValid() &&
            *current_version_p > *device_minimum_version_p) {
          // Do not update if the current version is newer than the minimum
          // version.
          LOG(INFO) << "Reading kiosk app required platform version failed "
                       "repeatedly but current version is newer than "
                       "DeviceMinimumVersion. Blocking update checks. "
                       "Current version: "
                    << *current_version_p
                    << " DeviceMinimumVersion: " << *device_minimum_version_p;
          return EvalStatus::kAskMeAgainLater;
        }
        LOG(WARNING) << "Reading kiosk app required platform version failed "
                        "repeatedly. Attempting an update without it now.";
        // An empty string for |target_version_prefix| allows arbitrary updates.
        result->target_version_prefix = "";
      } else {
        result->target_version_prefix = *kiosk_required_platform_version_p;
        LOG(INFO) << "Allow kiosk app to control Chrome version policy is set, "
                  << "target version is " << result->target_version_prefix;
      }
      // TODO(hunyadym): Add support for allowing rollback using the manifest
      // (if policy doesn't specify otherwise).
    } else {
      // Determine whether a target version prefix is dictated by policy.
      const string* target_version_prefix_p =
          ec->GetValue(dp_provider->var_target_version_prefix());
      if (target_version_prefix_p)
        result->target_version_prefix = *target_version_prefix_p;
    }

    // Determine whether a target channel is dictated by policy and whether we
    // should rollback in case that channel is more stable.
    const bool* release_channel_delegated_p =
        ec->GetValue(dp_provider->var_release_channel_delegated());
    if (release_channel_delegated_p && !(*release_channel_delegated_p)) {
      const string* release_channel_p =
          ec->GetValue(dp_provider->var_release_channel());
      if (release_channel_p) {
        result->target_channel = *release_channel_p;
        const ChannelDowngradeBehavior* channel_downgrade_behavior_p =
            ec->GetValue(dp_provider->var_channel_downgrade_behavior());
        if (channel_downgrade_behavior_p &&
            *channel_downgrade_behavior_p ==
                ChannelDowngradeBehavior::kRollback) {
          result->rollback_on_channel_downgrade = true;
        }
      }
    }
  }
  return EvalStatus::kContinue;
}

}  // namespace chromeos_update_manager
