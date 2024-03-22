// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_cellular.h"

#include <string>

#include <gmock/gmock.h>
#include <net-base/mac_address.h>

namespace shill {

MockCellular::MockCellular(Manager* manager,
                           const std::string& link_name,
                           net_base::MacAddress mac_address,
                           int interface_index,
                           const std::string& service,
                           const RpcIdentifier& path)
    : Cellular(
          manager, link_name, mac_address, interface_index, service, path) {}

MockCellular::~MockCellular() = default;

}  // namespace shill
