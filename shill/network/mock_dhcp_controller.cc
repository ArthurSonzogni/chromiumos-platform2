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
                     /*lease_file_suffix=*/"",
                     /*arp_gateway=*/false,
                     /*hostname=*/"",
                     Technology::kUnknown,
                     /*metrics=*/nullptr) {}

MockDHCPController::~MockDHCPController() = default;

void MockDHCPController::RegisterCallbacks(UpdateCallback update_callback,
                                           FailureCallback failure_callback) {
  update_callback_ = update_callback;
  failure_callback_ = failure_callback;
}

void MockDHCPController::TriggerUpdateCallback(
    const IPConfig::Properties& props) {
  update_callback_.Run(props, /*new_lease_acquired=*/true);
}

void MockDHCPController::TriggerFailureCallback() {
  failure_callback_.Run();
}

void MockDHCPController::ProcessEventSignal(
    const std::string& reason, const KeyValueStore& configuration) {}
}  // namespace shill
