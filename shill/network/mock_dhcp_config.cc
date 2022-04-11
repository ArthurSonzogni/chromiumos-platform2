// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_dhcp_config.h"

#include "shill/technology.h"

namespace shill {

MockDHCPConfig::MockDHCPConfig(ControlInterface* control_interface,
                               const std::string& device_name)
    : DHCPConfig(control_interface,
                 /*dispatcher=*/nullptr,
                 /*provider=*/nullptr,
                 device_name,
                 /*lease_file_suffix=*/"",
                 /*arp_gateway=*/false,
                 /*hostname=*/"",
                 Technology::kUnknown,
                 /*metrics=*/nullptr) {}

MockDHCPConfig::~MockDHCPConfig() = default;

void MockDHCPConfig::ProcessEventSignal(const std::string& reason,
                                        const KeyValueStore& configuration) {}
}  // namespace shill
