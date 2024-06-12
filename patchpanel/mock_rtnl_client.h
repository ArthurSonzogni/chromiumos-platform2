// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_RTNL_CLIENT_H_
#define PATCHPANEL_MOCK_RTNL_CLIENT_H_

#include "patchpanel/rtnl_client.h"

#include <map>
#include <optional>

#include <gmock/gmock.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/mac_address.h>

namespace patchpanel {

class MockRTNLClient : public RTNLClient {
 public:
  MockRTNLClient();
  explicit MockRTNLClient(const MockRTNLClient&) = delete;
  MockRTNLClient& operator=(const MockRTNLClient&) = delete;
  virtual ~MockRTNLClient();

  MOCK_METHOD((std::map<net_base::IPv4Address, net_base::MacAddress>),
              GetIPv4NeighborMacTable,
              (const std::optional<int>&),
              (const, override));
  MOCK_METHOD((std::map<net_base::IPv6Address, net_base::MacAddress>),
              GetIPv6NeighborMacTable,
              (const std::optional<int>&),
              (const, override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_RTNL_CLIENT_H_
