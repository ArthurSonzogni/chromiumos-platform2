// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/mock_wifi.h"

#include <memory>
#include <optional>
#include <string>

#include <chromeos/net-base/mac_address.h>

namespace shill {

MockWiFi::MockWiFi(Manager* manager,
                   const std::string& link_name,
                   std::optional<net_base::MacAddress> mac_address,
                   int interface_index,
                   uint32_t phy_index,
                   WakeOnWiFiInterface* wake_on_wifi)
    : WiFi(manager,
           link_name,
           mac_address,
           interface_index,
           phy_index,
           std::unique_ptr<WakeOnWiFiInterface>(wake_on_wifi)) {}

MockWiFi::~MockWiFi() = default;

}  // namespace shill
