// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_
#define SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_

#include <vector>

#include <gmock/gmock.h>
#include <net-base/ip_address.h>

#include "shill/network/network_monitor.h"

namespace shill {

class MockNetworkMonitor : public NetworkMonitor {
 public:
  MockNetworkMonitor();
  ~MockNetworkMonitor() override;

  MOCK_METHOD(bool,
              Start,
              (ValidationReason,
               net_base::IPFamily ip_family,
               const std::vector<net_base::IPAddress>& dns_list),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_
