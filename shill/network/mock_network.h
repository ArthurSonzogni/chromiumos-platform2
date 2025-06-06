// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_H_
#define SHILL_NETWORK_MOCK_NETWORK_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/time/time.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/network_priority.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <gmock/gmock.h>

#include "shill/network/network.h"
#include "shill/network/network_monitor.h"
#include "shill/technology.h"

namespace shill {

// TODO(b/182777518): Consider a fake implementation after we finish refactoring
// the Network class interface.
class MockNetwork : public Network {
 public:
  explicit MockNetwork(int interface_index,
                       const std::string& interface_name,
                       Technology technology);
  MockNetwork(const MockNetwork&) = delete;
  MockNetwork& operator=(const MockNetwork&) = delete;
  ~MockNetwork() override;

  MOCK_METHOD(void, Start, (const StartOptions&), (override));
  MOCK_METHOD(void, Stop, (), (override));

  MOCK_METHOD(bool, IsConnected, (), (const, override));
  MOCK_METHOD(bool, HasInternetConnectivity, (), (const, override));

  MOCK_METHOD(void,
              OnStaticIPConfigChanged,
              (const net_base::NetworkConfig&),
              (override));
  MOCK_METHOD(IPConfig*, GetCurrentIPConfig, (), (const, override));

  MOCK_METHOD(std::vector<net_base::IPCIDR>,
              GetAddresses,
              (),
              (const, override));
  MOCK_METHOD(std::vector<net_base::IPAddress>,
              GetDNSServers,
              (),
              (const, override));

  MOCK_METHOD(bool, RenewDHCPLease, (DHCPProvisionReason), (override));
  MOCK_METHOD(std::optional<base::TimeDelta>,
              TimeToNextDHCPLeaseRenewal,
              (),
              (override));

  MOCK_METHOD(void, InvalidateIPv6Config, (), (override));

  MOCK_METHOD(void, DestroySockets, (std::optional<uid_t>), (override));

  MOCK_METHOD(void, SetPriority, (net_base::NetworkPriority), (override));

  MOCK_METHOD(void,
              OnNeighborReachabilityEvent,
              (const patchpanel::Client::NeighborReachabilityEvent&));
  MOCK_METHOD(bool, ipv4_gateway_found, (), (const, override));
  MOCK_METHOD(void,
              UpdateNetworkValidationMode,
              (NetworkMonitor::ValidationMode mode),
              (override));
  MOCK_METHOD(void,
              RequestNetworkValidation,
              (NetworkMonitor::ValidationReason),
              (override));
  MOCK_METHOD(void, StopPortalDetection, (bool), (override));

  MOCK_METHOD(bool, IsConnectedViaTether, (), (const, override));
  MOCK_METHOD(void,
              OnTermsAndConditions,
              (const net_base::HttpUrl&),
              (override));
  MOCK_METHOD(int, network_id, (), (const, override));
  MOCK_METHOD(void,
              RequestTrafficCounters,
              (Network::GetTrafficCountersCallback),
              (override));
};

class MockNetworkEventHandler : public Network::EventHandler {
 public:
  MockNetworkEventHandler();
  MockNetworkEventHandler(const MockNetworkEventHandler&) = delete;
  MockNetworkEventHandler& operator=(const MockNetworkEventHandler&) = delete;
  ~MockNetworkEventHandler();

  MOCK_METHOD(void, OnConnectionUpdated, (int), (override));
  MOCK_METHOD(void, OnNetworkStopped, (int, bool), (override));
  MOCK_METHOD(void, OnIPConfigsPropertyUpdated, (int), (override));
  MOCK_METHOD(void, OnGetDHCPLease, (int), (override));
  MOCK_METHOD(void, OnGetDHCPFailure, (int), (override));
  MOCK_METHOD(void, OnGetSLAACAddress, (int), (override));
  MOCK_METHOD(void,
              OnNeighborReachabilityEvent,
              (int,
               const net_base::IPAddress&,
               patchpanel::Client::NeighborRole,
               patchpanel::Client::NeighborStatus));
  MOCK_METHOD(void, OnNetworkValidationStart, (int, bool), (override));
  MOCK_METHOD(void, OnNetworkValidationStop, (int, bool), (override));
  MOCK_METHOD(void,
              OnNetworkValidationResult,
              (int, const NetworkMonitor::Result& result),
              (override));
  MOCK_METHOD(void, OnNetworkDestroyed, (int, int), (override));
  MOCK_METHOD(void,
              OnTrafficCountersUpdate,
              (int, const Network::TrafficCounterMap&),
              (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_H_
