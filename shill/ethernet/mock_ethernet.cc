// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/mock_ethernet.h"

#include <net-base/mac_address.h>

namespace shill {

MockEthernet::MockEthernet(Manager* manager,
                           const std::string& link_name,
                           net_base::MacAddress mac_address,
                           int interface_index)
    : Ethernet(manager, link_name, mac_address, interface_index) {}

MockEthernet::~MockEthernet() = default;

}  // namespace shill
