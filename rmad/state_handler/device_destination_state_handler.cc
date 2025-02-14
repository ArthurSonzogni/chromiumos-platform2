// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/device_destination_state_handler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_utils.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/system/device_management_client_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback) {
  device_management_client_ = std::make_unique<DeviceManagementClientImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
}

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    std::unique_ptr<DeviceManagementClient> device_management_client,
    std::unique_ptr<WriteProtectUtils> write_protect_utils)
    : BaseStateHandler(json_store, daemon_callback),
      device_management_client_(std::move(device_management_client)),
      write_protect_utils_(std::move(write_protect_utils)) {}

RmadErrorCode DeviceDestinationStateHandler::InitializeState() {
  if (!state_.has_device_destination()) {
    state_.set_allocated_device_destination(new DeviceDestinationState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
DeviceDestinationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_device_destination()) {
    LOG(ERROR) << "RmadState missing |device destination| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }
  const DeviceDestinationState& device_destination = state.device_destination();
  if (device_destination.destination() ==
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |destination| argument.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
  }

  state_ = state;

  // There are 3 cases:
  // Same owner and replaced components don't require WP disabling (Case 1):
  //    Go to Finalize state.
  // Different owner or Replaced components require WP disabling, and
  //   - CCD blocked (Case 2):
  //     Go to kWpDisableRsu state. WP can only be disabled by RSU when CCD is
  //     blocked.
  //   - CCD not blocked (Case 3):
  //     Go to kWpDisableMethod state.
  //
  // We should set the following values to json_store:
  // - kSameOwner
  // - kWpDisableRequired
  // - kCcdBlocked (only required when kWpDisableRequired is true)
  bool is_same_user = device_destination.destination() ==
                      DeviceDestinationState::RMAD_DESTINATION_SAME;
  json_store_->SetValue(kSameOwner, is_same_user);
  if (is_same_user) {
    MetricsUtils::SetMetricsValue(
        json_store_, kMetricsReturningOwner,
        ReturningOwner_Name(ReturningOwner::RMAD_RETURNING_OWNER_SAME_OWNER));
    RecordDeviceDestinationToLogs(
        json_store_,
        ReturningOwner_Name(ReturningOwner::RMAD_RETURNING_OWNER_SAME_OWNER));
  } else {
    MetricsUtils::SetMetricsValue(
        json_store_, kMetricsReturningOwner,
        ReturningOwner_Name(
            ReturningOwner::RMAD_RETURNING_OWNER_DIFFERENT_OWNER));
    RecordDeviceDestinationToLogs(
        json_store_, ReturningOwner_Name(
                         ReturningOwner::RMAD_RETURNING_OWNER_DIFFERENT_OWNER));
  }

  if (is_same_user && !ReplacedComponentNeedHwwpDisabled()) {
    // Case 1.
    json_store_->SetValue(kWpDisableRequired, false);
    json_store_->SetValue(kWipeDevice, false);
    return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
  } else {
    json_store_->SetValue(kWpDisableRequired, true);
    json_store_->SetValue(kWipeDevice, true);

    if (device_management_client_->IsCcdBlocked()) {
      // Case 2.
      json_store_->SetValue(kCcdBlocked, true);
      return NextStateCaseWrapper(RmadState::StateCase::kWpDisableRsu);
    } else {
      // Case 3.
      json_store_->SetValue(kCcdBlocked, false);

      // If HWWP is already disabled or CHASSIS_OPEN signal is true, assume the
      // user will select the physical method and go directly to
      // WpDisablePhysical state.
      if (write_protect_utils_->ReadyForFactoryMode()) {
        return NextStateCaseWrapper(RmadState::StateCase::kWpDisablePhysical);
      }
      // Otherwise, let the user choose between physical method or RSU.
      return NextStateCaseWrapper(RmadState::StateCase::kWpDisableMethod);
    }
  }
}

bool DeviceDestinationStateHandler::ReplacedComponentNeedHwwpDisabled() const {
  if (bool mlb_repair;
      json_store_->GetValue(kMlbRepair, &mlb_repair) && mlb_repair) {
    // MLB repair implies "different owner" so this state should be skipped.
    // Still adding this check just for safety.
    return true;
  }
  if (std::vector<std::string> replaced_component_names; json_store_->GetValue(
          kReplacedComponentNames, &replaced_component_names)) {
    for (const std::string& component_name : replaced_component_names) {
      RmadComponent component;
      CHECK(RmadComponent_Parse(component_name, &component));
      if (kComponentsNeedManualCalibration.contains(component) ||
          kComponentsNeedUpdateCbi.contains(component)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace rmad
