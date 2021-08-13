// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/manager.h"
#include "hermes/hermes_common.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/logging.h>
#include <brillo/errors/error_codes.h>
#include <google-lpa/lpa/core/lpa.h>

namespace {

std::string LogicalSlotToStr(base::Optional<uint8_t> logical_slot) {
  return logical_slot ? std::to_string(logical_slot.value()) : "None";
}

}  // namespace

namespace hermes {

Manager::Manager()
    : context_(Context::Get()),
      dbus_adaptor_(context_->adaptor_factory()->CreateManagerAdaptor(this)) {}

void Manager::OnEuiccUpdated(uint8_t physical_slot, EuiccSlotInfo slot_info) {
  LOG(INFO) << __func__ << " physical_slot: " << physical_slot
            << " eid(Last 3 chars): " << GetTrailingChars(slot_info.eid(), 3)
            << " logical_slot: " << LogicalSlotToStr(slot_info.logical_slot());
  auto iter = available_euiccs_.find(physical_slot);
  if (iter == available_euiccs_.end()) {
    available_euiccs_[physical_slot] =
        std::make_unique<Euicc>(physical_slot, std::move(slot_info));
    UpdateAvailableEuiccsProperty();
    return;
  }

  iter->second->UpdateSlotInfo(std::move(slot_info));
}

void Manager::OnEuiccRemoved(uint8_t physical_slot) {
  LOG(INFO) << __func__ << " physical_slot: " << physical_slot;
  auto iter = available_euiccs_.find(physical_slot);
  if (iter == available_euiccs_.end()) {
    return;
  }
  available_euiccs_.erase(iter);
  UpdateAvailableEuiccsProperty();
}

void Manager::UpdateAvailableEuiccsProperty() {
  LOG(INFO) << __func__;
  std::vector<dbus::ObjectPath> euicc_paths;
  for (const auto& euicc : available_euiccs_) {
    euicc_paths.push_back(euicc.second->object_path());
  }
  dbus_adaptor_->SetAvailableEuiccs(euicc_paths);
}

void Manager::OnLogicalSlotUpdated(uint8_t physical_slot,
                                   base::Optional<uint8_t> logical_slot) {
  LOG(INFO) << __func__ << " physical_slot: " << physical_slot
            << " logical_slot: " << LogicalSlotToStr(logical_slot);
  auto iter = available_euiccs_.find(physical_slot);
  if (iter == available_euiccs_.end()) {
    VLOG(2) << "Ignoring logical slot change for non-eUICC physical slot:"
            << physical_slot;
    return;
  }

  iter->second->UpdateLogicalSlot(std::move(logical_slot));
}

}  // namespace hermes
