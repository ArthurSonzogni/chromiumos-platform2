// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_legacy_dhcp_controller.h"

#include "shill/technology.h"

namespace shill {

MockLegacyDHCPController::MockLegacyDHCPController(
    ControlInterface* control_interface, const std::string& device_name)
    : LegacyDHCPController(control_interface,
                           /*dispatcher=*/nullptr,
                           /*provider=*/nullptr,
                           device_name,
                           /*options=*/{},
                           Technology::kUnknown,
                           /*metrics=*/nullptr) {}

MockLegacyDHCPController::~MockLegacyDHCPController() = default;

void MockLegacyDHCPController::RegisterCallbacks(UpdateCallback update_callback,
                                                 DropCallback drop_callback) {
  update_callback_ = update_callback;
  drop_callback_ = drop_callback;
}

void MockLegacyDHCPController::TriggerUpdateCallback(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data) {
  update_callback_.Run(network_config, dhcp_data, /*new_lease_acquired=*/true);
}

void MockLegacyDHCPController::TriggerDropCallback(bool is_voluntary) {
  drop_callback_.Run(is_voluntary);
}

void MockLegacyDHCPController::ProcessEventSignal(
    ClientEventReason reason, const KeyValueStore& configuration) {}
}  // namespace shill
