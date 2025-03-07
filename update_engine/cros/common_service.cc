// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/common_service.h"

#include <string>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/strings/string_utils.h>
#include <policy/device_policy.h>
#include <update_engine/dbus-constants.h>

#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/connection_manager_interface.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/omaha_utils.h"
#include "update_engine/cros/p2p_manager.h"
#include "update_engine/cros/payload_state_interface.h"
#include "update_engine/cros/update_attempter.h"

using base::StringPrintf;
using brillo::ErrorPtr;
using brillo::string_utils::ToString;
using std::string;
using std::vector;
using update_engine::UpdateAttemptFlags;
using update_engine::UpdateEngineStatus;

namespace chromeos_update_engine {

namespace {
// Log and set the error on the passed ErrorPtr.
void LogAndSetError(ErrorPtr* error,
                    const base::Location& location,
                    const string& reason) {
  brillo::Error::AddTo(error, location, UpdateEngineService::kErrorDomain,
                       UpdateEngineService::kErrorFailed, reason);
  LOG(ERROR) << "Sending Update Engine Failure: " << location.ToString() << ": "
             << reason;
}
}  // namespace

const char* const UpdateEngineService::kErrorDomain = "update_engine";
const char* const UpdateEngineService::kErrorFailed =
    "org.chromium.UpdateEngine.Error.Failed";

UpdateEngineService::UpdateEngineService() = default;

// org::chromium::UpdateEngineInterfaceInterface methods implementation.

bool UpdateEngineService::Update(
    ErrorPtr* /* error */,
    const update_engine::UpdateParams& update_params,
    bool* out_result) {
  LOG(INFO) << "Update: app_version=\"" << update_params.app_version() << "\" "
            << "omaha_url=\"" << update_params.omaha_url() << "\" "
            << "interactive="
            << (update_params.update_flags().non_interactive() ? "no" : "yes");
  *out_result =
      SystemState::Get()->update_attempter()->CheckForUpdate(update_params);
  return true;
}

bool UpdateEngineService::ApplyDeferredUpdate(ErrorPtr* error, bool shutdown) {
  if (!SystemState::Get()->update_attempter()->ApplyDeferredUpdate(shutdown)) {
    LogAndSetError(error, FROM_HERE, "Failed to apply deferred update.");
    return false;
  }
  return true;
}

bool UpdateEngineService::AttemptInstall(brillo::ErrorPtr* error,
                                         const string& omaha_url,
                                         const vector<string>& dlc_ids) {
  if (!SystemState::Get()->update_attempter()->CheckForInstall(
          dlc_ids, omaha_url,
          /*scaled=*/false)) {
    // TODO(xiaochu): support more detailed error messages.
    LogAndSetError(error, FROM_HERE, "Could not schedule install.");
    return false;
  }
  return true;
}

bool UpdateEngineService::Install(
    brillo::ErrorPtr* error,
    const update_engine::InstallParams& install_params) {
  if (!SystemState::Get()->update_attempter()->CheckForInstall(
          {install_params.id()}, install_params.omaha_url(),
          install_params.scaled(), install_params.force_ota())) {
    LogAndSetError(error, FROM_HERE, "Could not schedule scaled install.");
    return false;
  }
  return true;
}

bool UpdateEngineService::Migrate(brillo::ErrorPtr* error) {
  if (!SystemState::Get()->update_attempter()->CheckForInstall(
          {}, /*omaha_url=*/"", /*scaled=*/true, /*force_ota=*/false,
          /*migration=*/true)) {
    LogAndSetError(error, FROM_HERE, "Could not schedule migration install.");
    return false;
  }
  return true;
}

bool UpdateEngineService::AttemptRollback(ErrorPtr* error, bool in_powerwash) {
  LOG(INFO) << "Attempting rollback to non-active partitions.";

  if (!SystemState::Get()->update_attempter()->Rollback(in_powerwash)) {
    // TODO(dgarrett): Give a more specific error code/reason.
    LogAndSetError(error, FROM_HERE, "Rollback attempt failed.");
    return false;
  }
  return true;
}

bool UpdateEngineService::CanRollback(ErrorPtr* /* error */,
                                      bool* out_can_rollback) {
  bool can_rollback = SystemState::Get()->update_attempter()->CanRollback();
  LOG(INFO) << "Checking to see if we can rollback . Result: " << can_rollback;
  *out_can_rollback = can_rollback;
  return true;
}

bool UpdateEngineService::ResetStatus(ErrorPtr* error) {
  if (!SystemState::Get()->update_attempter()->ResetStatus()) {
    // TODO(dgarrett): Give a more specific error code/reason.
    LogAndSetError(error, FROM_HERE, "ResetStatus failed.");
    return false;
  }
  return true;
}

bool UpdateEngineService::SetDlcActiveValue(brillo::ErrorPtr* error,
                                            bool is_active,
                                            const string& dlc_id) {
  if (!SystemState::Get()->update_attempter()->SetDlcActiveValue(is_active,
                                                                 dlc_id)) {
    LogAndSetError(error, FROM_HERE, "SetDlcActiveValue failed.");
    return false;
  }
  return true;
}

bool UpdateEngineService::GetStatus(ErrorPtr* error,
                                    UpdateEngineStatus* out_status) {
  if (!SystemState::Get()->update_attempter()->GetStatus(out_status)) {
    LogAndSetError(error, FROM_HERE, "GetStatus failed.");
    return false;
  }
  return true;
}

bool UpdateEngineService::SetStatus(brillo::ErrorPtr*,
                                    update_engine::UpdateStatus status) {
  SystemState::Get()->update_attempter()->SetStatusAndNotify(status);
  return true;
}

bool UpdateEngineService::RebootIfNeeded(ErrorPtr* error) {
  if (!SystemState::Get()->update_attempter()->RebootIfNeeded()) {
    // TODO(dgarrett): Give a more specific error code/reason.
    LogAndSetError(error, FROM_HERE, "Reboot not needed, or attempt failed.");
    return false;
  }
  return true;
}

bool UpdateEngineService::SetChannel(ErrorPtr* error,
                                     const string& in_target_channel,
                                     bool in_is_powerwash_allowed) {
  const policy::DevicePolicy* device_policy =
      SystemState::Get()->device_policy();

  // The device_policy is loaded in a lazy way before an update check. Load it
  // now from the libbrillo cache if it wasn't already loaded.
  if (!device_policy) {
    UpdateAttempter* update_attempter = SystemState::Get()->update_attempter();
    if (update_attempter) {
      update_attempter->RefreshDevicePolicy();
      device_policy = SystemState::Get()->device_policy();
    }
  }

  bool delegated = false;
  if (device_policy && device_policy->GetReleaseChannelDelegated(&delegated) &&
      !delegated) {
    LogAndSetError(error, FROM_HERE,
                   "Cannot set target channel explicitly when channel "
                   "policy/settings is not delegated");
    return false;
  }

  if (OmahaRequestParams::IsCommercialChannel(in_target_channel)) {
    LogAndSetError(error, FROM_HERE,
                   "Cannot set a commercial channel explicitly");
    return false;
  }

  LOG(INFO) << "Setting destination channel to: " << in_target_channel;
  string error_message;
  if (!SystemState::Get()->request_params()->SetTargetChannel(
          in_target_channel, in_is_powerwash_allowed, &error_message)) {
    LogAndSetError(error, FROM_HERE, error_message);
    return false;
  }
  return true;
}

bool UpdateEngineService::GetChannel(ErrorPtr* /* error */,
                                     bool in_get_current_channel,
                                     string* out_channel) {
  OmahaRequestParams* rp = SystemState::Get()->request_params();
  *out_channel =
      (in_get_current_channel ? rp->current_channel() : rp->target_channel());
  return true;
}

bool UpdateEngineService::SetCohortHint(ErrorPtr* error,
                                        const string& in_cohort_hint) {
  // It is ok to override the cohort hint with an invalid value since it is
  // stored in stateful partition. The code reading it should sanitize it
  // anyway.
  if (!SystemState::Get()->prefs()->SetString(kPrefsOmahaCohortHint,
                                              in_cohort_hint)) {
    LogAndSetError(
        error, FROM_HERE,
        StringPrintf("Error setting the cohort hint value to \"%s\".",
                     in_cohort_hint.c_str()));
    return false;
  }
  return true;
}

bool UpdateEngineService::GetCohortHint(ErrorPtr* error,
                                        string* out_cohort_hint) {
  const auto* prefs = SystemState::Get()->prefs();
  *out_cohort_hint = "";
  if (prefs->Exists(kPrefsOmahaCohortHint) &&
      !prefs->GetString(kPrefsOmahaCohortHint, out_cohort_hint)) {
    LogAndSetError(error, FROM_HERE, "Error getting the cohort hint.");
    return false;
  }
  return true;
}

bool UpdateEngineService::SetP2PUpdatePermission(ErrorPtr* error,
                                                 bool in_enabled) {
  if (!SystemState::Get()->prefs()->SetBoolean(kPrefsP2PEnabled, in_enabled)) {
    LogAndSetError(
        error, FROM_HERE,
        StringPrintf("Error setting the update via p2p permission to %s.",
                     ToString(in_enabled).c_str()));
    return false;
  }
  return true;
}

bool UpdateEngineService::GetP2PUpdatePermission(ErrorPtr* error,
                                                 bool* out_enabled) {
  const auto* prefs = SystemState::Get()->prefs();
  bool p2p_pref = false;  // Default if no setting is present.
  if (prefs->Exists(kPrefsP2PEnabled) &&
      !prefs->GetBoolean(kPrefsP2PEnabled, &p2p_pref)) {
    LogAndSetError(error, FROM_HERE, "Error getting the P2PEnabled setting.");
    return false;
  }

  *out_enabled = p2p_pref;
  return true;
}

bool UpdateEngineService::SetUpdateOverCellularPermission(ErrorPtr* error,
                                                          bool in_allowed) {
  ConnectionManagerInterface* connection_manager =
      SystemState::Get()->connection_manager();

  // Check if this setting is allowed by the device policy.
  if (connection_manager->IsAllowedConnectionTypesForUpdateSet()) {
    LogAndSetError(error, FROM_HERE,
                   "Ignoring the update over cellular setting since there's "
                   "a device policy enforcing this setting.");
    return false;
  }

  // If the policy wasn't loaded yet, then it is still OK to change the local
  // setting because the policy will be checked again during the update check.
  if (!SystemState::Get()->prefs()->SetBoolean(
          kPrefsUpdateOverCellularPermission, in_allowed)) {
    LogAndSetError(error, FROM_HERE,
                   string("Error setting the update over cellular to ") +
                       (in_allowed ? "true" : "false"));
    return false;
  }
  return true;
}

bool UpdateEngineService::SetUpdateOverCellularTarget(
    brillo::ErrorPtr* error,
    const std::string& target_version,
    int64_t target_size) {
  ConnectionManagerInterface* connection_manager =
      SystemState::Get()->connection_manager();

  // Check if this setting is allowed by the device policy.
  if (connection_manager->IsAllowedConnectionTypesForUpdateSet()) {
    LogAndSetError(error, FROM_HERE,
                   "Ignoring the update over cellular setting since there's "
                   "a device policy enforcing this setting.");
    return false;
  }

  // If the policy wasn't loaded yet, then it is still OK to change the local
  // setting because the policy will be checked again during the update check.

  auto* prefs = SystemState::Get()->prefs();
  if (!prefs->SetString(kPrefsUpdateOverCellularTargetVersion,
                        target_version) ||
      !prefs->SetInt64(kPrefsUpdateOverCellularTargetSize, target_size)) {
    LogAndSetError(error, FROM_HERE,
                   "Error setting the target for update over cellular.");
    return false;
  }
  return true;
}

bool UpdateEngineService::GetUpdateOverCellularPermission(ErrorPtr* error,
                                                          bool* out_allowed) {
  ConnectionManagerInterface* connection_manager =
      SystemState::Get()->connection_manager();

  if (connection_manager->IsAllowedConnectionTypesForUpdateSet()) {
    // We have device policy, so ignore the user preferences.
    *out_allowed = connection_manager->IsUpdateAllowedOverMetered();
  } else {
    const auto* prefs = SystemState::Get()->prefs();
    if (!prefs->Exists(kPrefsUpdateOverCellularPermission)) {
      // Update is not allowed as user preference is not set or not available.
      *out_allowed = false;
      return true;
    }

    bool is_allowed;

    if (!prefs->GetBoolean(kPrefsUpdateOverCellularPermission, &is_allowed)) {
      LogAndSetError(error, FROM_HERE,
                     "Error getting the update over cellular preference.");
      return false;
    }
    *out_allowed = is_allowed;
  }
  return true;
}

bool UpdateEngineService::ToggleFeature(ErrorPtr* error,
                                        const std::string& feature,
                                        bool enable) {
  if (SystemState::Get()->update_attempter()->ToggleFeature(feature, enable)) {
    return true;
  }
  LogAndSetError(error, FROM_HERE,
                 string("Failed to toggle feature ") + feature);
  return false;
}

bool UpdateEngineService::IsFeatureEnabled(ErrorPtr* error,
                                           const std::string& feature,
                                           bool* out_enabled) {
  if (SystemState::Get()->update_attempter()->IsFeatureEnabled(feature,
                                                               out_enabled)) {
    return true;
  }
  LogAndSetError(error, FROM_HERE, string("Failed to get feature ") + feature);
  return false;
}

bool UpdateEngineService::GetDurationSinceUpdate(ErrorPtr* error,
                                                 int64_t* out_usec_wallclock) {
  base::Time time;
  if (!SystemState::Get()->update_attempter()->GetBootTimeAtUpdate(&time)) {
    LogAndSetError(error, FROM_HERE, "No pending update.");
    return false;
  }

  const auto* clock = SystemState::Get()->clock();
  *out_usec_wallclock = (clock->GetBootTime() - time).InMicroseconds();
  return true;
}

bool UpdateEngineService::GetPrevVersion(ErrorPtr* /* error */,
                                         string* out_prev_version) {
  *out_prev_version = SystemState::Get()->update_attempter()->GetPrevVersion();
  return true;
}

bool UpdateEngineService::GetRollbackPartition(
    ErrorPtr* /* error */, string* out_rollback_partition_name) {
  BootControlInterface::Slot rollback_slot =
      SystemState::Get()->update_attempter()->GetRollbackSlot();

  if (rollback_slot == BootControlInterface::kInvalidSlot) {
    out_rollback_partition_name->clear();
    return true;
  }

  string name;
  if (!SystemState::Get()->boot_control()->GetPartitionDevice(
          "KERNEL", rollback_slot, &name)) {
    LOG(ERROR) << "Invalid rollback device";
    return false;
  }

  LOG(INFO) << "Getting rollback partition name. Result: " << name;
  *out_rollback_partition_name = name;
  return true;
}

bool UpdateEngineService::GetLastAttemptError(ErrorPtr* /* error */,
                                              int32_t* out_last_attempt_error) {
  ErrorCode error_code =
      SystemState::Get()->update_attempter()->GetAttemptErrorCode();
  *out_last_attempt_error = static_cast<int>(error_code);
  return true;
}

}  // namespace chromeos_update_engine
