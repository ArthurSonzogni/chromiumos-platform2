// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/mock_hotspot_device.h"

#include <string>

#include <chromeos/net-base/mac_address.h>

namespace shill {

MockHotspotDevice::MockHotspotDevice(Manager* manager,
                                     const std::string& primary_link_name,
                                     const std::string& link_name,
                                     net_base::MacAddress mac_address,
                                     uint32_t phy_index,
                                     WiFiPhy::Priority priority,
                                     const EventCallback& callback)
    : HotspotDevice(manager,
                    primary_link_name,
                    link_name,
                    mac_address,
                    phy_index,
                    priority,
                    callback) {}

MockHotspotDevice::~MockHotspotDevice() = default;

}  // namespace shill
