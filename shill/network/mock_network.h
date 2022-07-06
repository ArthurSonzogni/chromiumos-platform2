// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_H_
#define SHILL_NETWORK_MOCK_NETWORK_H_

#include <string>
#include <vector>

#include <base/callback.h>
#include <gmock/gmock.h>

#include "shill/ipconfig.h"
#include "shill/network/network.h"
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
  ~MockNetwork() override = default;

  MOCK_METHOD(bool, HasConnectionObject, (), (const, override));
  MOCK_METHOD(void,
              OnStaticIPConfigChanged,
              (const NetworkConfig&),
              (override));
  MOCK_METHOD(void,
              RegisterCurrentIPConfigChangeHandler,
              (base::RepeatingClosure),
              (override));
  MOCK_METHOD(IPConfig*, GetCurrentIPConfig, (), (const, override));

  MOCK_METHOD(bool,
              SetIPFlag,
              (IPAddress::Family, const std::string&, const std::string&),
              (override));

  MOCK_METHOD(bool, IsDefault, (), (const, override));
  MOCK_METHOD(void, SetPriority, (uint32_t, bool), (override));
  MOCK_METHOD(void, SetUseDNS, (bool), (override));
  MOCK_METHOD(std::string, GetSubnetName, (), (const, override));

  MOCK_METHOD(const std::vector<std::string>&,
              dns_servers,
              (),
              (const, override));
  MOCK_METHOD(const IPAddress&, local, (), (const, override));
};

class MockNetworkEventHandler : public Network::EventHandler {
 public:
  MOCK_METHOD(void, OnConnectionUpdated, (IPConfig*), (override));
  MOCK_METHOD(void, OnIPConfigsPropertyUpdated, (), (override));
  MOCK_METHOD(std::vector<uint32_t>, GetBlackholedUids, (), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_H_
