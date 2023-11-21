// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_
#define SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_

#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "shill/network/network_applier.h"

namespace shill {

class MockNetworkApplier : public NetworkApplier {
 public:
  MockNetworkApplier();
  MockNetworkApplier(const MockNetworkApplier&) = delete;
  MockNetworkApplier& operator=(const MockNetworkApplier&) = delete;
  ~MockNetworkApplier() override;

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, ApplyMTU, (int, int), (override));
  MOCK_METHOD(void,
              ApplyAddress,
              (int,
               const net_base::IPCIDR&,
               const std::optional<net_base::IPv4Address>&),
              (override));
  MOCK_METHOD(void,
              ApplyRoute,
              (int,
               net_base::IPFamily,
               const std::optional<net_base::IPAddress>&,
               bool,
               bool,
               bool,
               const std::vector<net_base::IPCIDR>&,
               const std::vector<net_base::IPCIDR>&,
               (const std::vector<
                   std::pair<net_base::IPv4CIDR, net_base::IPv4Address>>&)),
              (override));
  MOCK_METHOD(void,
              ApplyRoutingPolicy,
              (int interface_index,
               const std::string& interface_name,
               Technology technology,
               NetworkPriority priority,
               const std::vector<net_base::IPCIDR>& all_addresses,
               const std::vector<net_base::IPv4CIDR>& rfc3442_dsts),
              (override));
  MOCK_METHOD(void,
              ApplyDNS,
              (NetworkPriority priority,
               const std::vector<net_base::IPAddress>& dns_servers,
               const std::vector<std::string>& dns_search_domains),
              (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_
