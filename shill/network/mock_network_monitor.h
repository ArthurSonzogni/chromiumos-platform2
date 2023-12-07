// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_
#define SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_

#include <memory>
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
               net_base::IPFamily,
               const std::vector<net_base::IPAddress>&),
              (override));
  MOCK_METHOD(bool, Stop, (), (override));
};

class MockNetworkMonitorFactory : public NetworkMonitorFactory {
 public:
  MockNetworkMonitorFactory();
  ~MockNetworkMonitorFactory() override;

  MOCK_METHOD(std::unique_ptr<NetworkMonitor>,
              Create,
              (EventDispatcher*,
               std::string_view,
               PortalDetector::ProbingConfiguration,
               NetworkMonitor::ResultCallback,
               std::string_view),
              (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_MONITOR_H_
