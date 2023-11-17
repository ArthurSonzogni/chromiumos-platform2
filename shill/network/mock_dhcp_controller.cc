// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_dhcp_controller.h"

#include "shill/technology.h"

namespace shill {

MockDHCPController::MockDHCPController(ControlInterface* control_interface,
                                       const std::string& device_name)
    : DHCPController(control_interface,
                     /*dispatcher=*/nullptr,
                     /*provider=*/nullptr,
                     device_name,
                     /*options=*/{},
                     Technology::kUnknown,
                     /*metrics=*/nullptr) {}

MockDHCPController::~MockDHCPController() = default;

void MockDHCPController::RegisterCallbacks(UpdateCallback update_callback,
                                           DropCallback drop_callback) {
  update_callback_ = update_callback;
  drop_callback_ = drop_callback;
}

void MockDHCPController::TriggerUpdateCallback(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data) {
  update_callback_.Run(network_config, dhcp_data, /*new_lease_acquired=*/true);
}

void MockDHCPController::TriggerDropCallback(bool is_voluntary) {
  drop_callback_.Run(is_voluntary);
}

void MockDHCPController::ProcessEventSignal(
    ClientEventReason reason, const KeyValueStore& configuration) {}
}  // namespace shill
