// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

namespace shill {
// Constructor function
HotspotDevice::HotspotDevice(Manager* manager,
                             const std::string& link_name,
                             const std::string& mac_address,
                             uint32_t phy_index,
                             LocalDevice::EventCallback callback)
    : LocalDevice(manager,
                  IfaceType::kAP,
                  link_name,
                  mac_address,
                  phy_index,
                  callback) {}

HotspotDevice::~HotspotDevice() {}

bool HotspotDevice::Start() {
  // TODO(b/246571884) Add virtual interface create method.
  return true;
}
bool HotspotDevice::Stop() {
  // TODO(b/246571884) Add virtual interface delete method.
  return true;
}

}  // namespace shill
