// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/state_handler_manager.h"

#include <utility>

#include <base/check.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/state_handler/calibrate_components_state_handler.h"
#include "rmad/state_handler/components_repair_state_handler.h"
#include "rmad/state_handler/device_destination_state_handler.h"
#include "rmad/state_handler/finalize_state_handler.h"
#include "rmad/state_handler/provision_device_state_handler.h"
#include "rmad/state_handler/restock_state_handler.h"
#include "rmad/state_handler/select_network_state_handler.h"
#include "rmad/state_handler/update_chrome_state_handler.h"
#include "rmad/state_handler/update_device_info_state_handler.h"
#include "rmad/state_handler/update_ro_firmware_state_handler.h"
#include "rmad/state_handler/welcome_screen_state_handler.h"
#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"
#include "rmad/state_handler/write_protect_disable_method_state_handler.h"
#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"
#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"
#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

namespace rmad {

StateHandlerManager::StateHandlerManager(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

void StateHandlerManager::RegisterStateHandler(
    scoped_refptr<BaseStateHandler> handler) {
  RmadState::StateCase state = handler->GetStateCase();
  auto res = state_handler_map_.insert(std::make_pair(state, handler));
  // Check if there are StateCase collisions.
  DCHECK(res.second) << "Registered handlers should have unique RmadStates.";
}

void StateHandlerManager::RegisterStateHandlers() {
  // TODO(gavindodd): Some form of validation of state loaded from the store is
  // needed. e.g. RMA abortable state must match what is expected by the
  // current position in the state flow, but depends on some state in the
  // history.
  // Maybe initializing states in history order would help?
  RegisterStateHandler(
      base::MakeRefCounted<WelcomeScreenStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<SelectNetworkStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<UpdateChromeStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<ComponentsRepairStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<DeviceDestinationStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<WriteProtectDisableMethodStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<WriteProtectDisableRsuStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<WriteProtectDisablePhysicalStateHandler>(
          json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
          json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<UpdateRoFirmwareStateHandler>(json_store_));
  RegisterStateHandler(base::MakeRefCounted<RestockStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<UpdateDeviceInfoStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<CalibrateComponentsStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<ProvisionDeviceStateHandler>(json_store_));
  RegisterStateHandler(
      base::MakeRefCounted<WriteProtectEnablePhysicalStateHandler>(
          json_store_));
  RegisterStateHandler(base::MakeRefCounted<FinalizeStateHandler>(json_store_));
}

scoped_refptr<BaseStateHandler> StateHandlerManager::GetStateHandler(
    RmadState::StateCase state) const {
  auto it = state_handler_map_.find(state);
  if (it == state_handler_map_.end()) {
    // Unregistered RmadState. Return a null pointer.
    return scoped_refptr<BaseStateHandler>(nullptr);
  }
  return it->second;
}

}  // namespace rmad
