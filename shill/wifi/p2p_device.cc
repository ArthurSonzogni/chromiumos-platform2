// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <optional>
#include <string>

#include "shill/manager.h"

namespace shill {

// Constructor function
P2PDevice::P2PDevice(Manager* manager,
                     LocalDevice::IfaceType iface_type,
                     const std::string& primary_link_name,
                     uint32_t phy_index,
                     LocalDevice::EventCallback callback)
    : LocalDevice(manager, iface_type, std::nullopt, phy_index, callback),
      primary_link_name_(primary_link_name) {
  // A P2PDevice with a non-P2P interface type makes no sense.
  CHECK(iface_type == LocalDevice::IfaceType::kP2PGO ||
        iface_type == LocalDevice::IfaceType::kP2PClient);
}

P2PDevice::~P2PDevice() {}

bool P2PDevice::Start() {
  return true;
}

bool P2PDevice::Stop() {
  return true;
}

}  // namespace shill
