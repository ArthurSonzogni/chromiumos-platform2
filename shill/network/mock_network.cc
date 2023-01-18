// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_network.h"

#include <string>

#include "shill/mock_connection.h"
#include "shill/network/network.h"
#include "shill/technology.h"

namespace shill {

MockNetwork::MockNetwork(int interface_index,
                         const std::string& interface_name,
                         Technology technology)
    : Network(interface_index,
              interface_name,
              technology,
              /*fixed_ip_params=*/false,
              /*event_handler=*/nullptr,
              /*control_interface=*/nullptr,
              /*device_info=*/nullptr,
              /*dispatcher=*/nullptr,
              /*metrics=*/nullptr) {}

}  // namespace shill
