// Copyright 2021 The Chromium OS Authors. All rights reserved.
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
#include "rmad/metrics/metrics_constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/system/cryptohome_client_impl.h"
#include "rmad/system/fake_cryptohome_client.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cr50_utils.h"

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;

namespace fake {

FakeDeviceDestinationStateHandler::FakeDeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : DeviceDestinationStateHandler(
          json_store,
          std::make_unique<FakeCryptohomeClient>(working_dir_path),
          std::make_unique<FakeCr50Utils>(working_dir_path)) {}

}  // namespace fake

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cryptohome_client_ = std::make_unique<CryptohomeClientImpl>(GetSystemBus());
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
}

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<CryptohomeClient> cryptohome_client,
    std::unique_ptr<Cr50Utils> cr50_utils)
    : BaseStateHandler(json_store),
      cryptohome_client_(std::move(cryptohome_client)),
      cr50_utils_(std::move(cr50_utils)) {}

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

  // There are 5 paths:
  // 1. Same owner + replaced components require WP disabling + CCD blocked:
  //    Go to WipeSelection state.
  // 2. Same owner + replaced components require WP disabling + CCD not blocked:
  //    Go to WipeSelection state.
  // 3. Same owner + replaced components don't require WP disabling:
  //    Go to WipeSelection state.
  // 4. Different owner + CCD blocked:
  //    Mandatory RSU. Go straight to RSU state.
  // 5. Different owner + CCD not blocked:
  //    Must wipe the device. Skip WipeSelection state and go to WpDisableMethod
  //    state.
  // We should set the following values to json_store:
  // - kSameOwner
  // - kWpDisableRequired
  // - kCcdBlocked (only required when kWpDisableRequired is true)
  if (device_destination.destination() ==
      DeviceDestinationState::RMAD_DESTINATION_SAME) {
    json_store_->SetValue(kSameOwner, true);
    if (ReplacedComponentNeedHwwpDisabled()) {
      json_store_->SetValue(kWpDisableRequired, true);
      if (cryptohome_client_->IsCcdBlocked()) {
        // Case 1.
        json_store_->SetValue(kCcdBlocked, true);
        return NextStateCaseWrapper(RmadState::StateCase::kWipeSelection);
      } else {
        // Case 2.
        json_store_->SetValue(kCcdBlocked, false);
        return NextStateCaseWrapper(RmadState::StateCase::kWipeSelection);
      }
    } else {
      // Case 3.
      json_store_->SetValue(kWpDisableRequired, false);
      return NextStateCaseWrapper(RmadState::StateCase::kWipeSelection);
    }
  } else {
    json_store_->SetValue(kSameOwner, false);
    json_store_->SetValue(kWpDisableRequired, true);
    json_store_->SetValue(kWipeDevice, true);
    if (cryptohome_client_->IsCcdBlocked()) {
      // Case 4.
      json_store_->SetValue(kCcdBlocked, true);
      return NextStateCaseWrapper(RmadState::StateCase::kWpDisableRsu);
    } else {
      // Case 5.
      // If factory mode is already enabled, go directly to WpDisableComplete
      // state.
      json_store_->SetValue(kCcdBlocked, false);
      if (cr50_utils_->IsFactoryModeEnabled()) {
        json_store_->SetValue(kWpDisableSkipped, true);
        json_store_->SetValue(
            kWriteProtectDisableMethod,
            static_cast<int>(WriteProtectDisableMethod::SKIPPED));
        return NextStateCaseWrapper(RmadState::StateCase::kWpDisableComplete);
      }
      return NextStateCaseWrapper(RmadState::StateCase::kWpDisableMethod);
    }
  }
}

bool DeviceDestinationStateHandler::ReplacedComponentNeedHwwpDisabled() const {
  std::vector<std::string> replaced_component_names;
  if (!json_store_->GetValue(kReplacedComponentNames,
                             &replaced_component_names)) {
    return false;
  }
  // Using set union/intersection is faster, but since the number of components
  // is small, we keep it simple by using linear searches for each replaced
  // component. It shouldn't affect performance a lot.
  for (const std::string& component_name : replaced_component_names) {
    RmadComponent component;
    DCHECK(RmadComponent_Parse(component_name, &component));
    if (std::find(kComponentsNeedManualCalibration.begin(),
                  kComponentsNeedManualCalibration.end(),
                  component) != kComponentsNeedManualCalibration.end()) {
      return true;
    }
    if (std::find(kComponentsNeedUpdateCbi.begin(),
                  kComponentsNeedUpdateCbi.end(),
                  component) != kComponentsNeedUpdateCbi.end()) {
      return true;
    }
  }
  return false;
}

}  // namespace rmad
