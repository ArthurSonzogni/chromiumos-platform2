// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NETWORK_MOCK_ADDRESS_SERVICE_H_
#define PATCHPANEL_NETWORK_MOCK_ADDRESS_SERVICE_H_

#include <optional>
#include <vector>

#include <gmock/gmock.h>

#include "patchpanel/network/address_service.h"

namespace patchpanel {

class MockAddressService : public AddressService {
 public:
  MockAddressService();
  MockAddressService(const MockAddressService&) = delete;
  MockAddressService& operator=(const MockAddressService&) = delete;
  ~MockAddressService() override;

  MOCK_METHOD(void, FlushAddress, (int), (override));

  MOCK_METHOD(void,
              SetIPv4Address,
              (int interface_index,
               const net_base::IPv4CIDR& local,
               const std::optional<net_base::IPv4Address>& broadcast),
              (override));

  MOCK_METHOD(void, ClearIPv4Address, (int interface_index), (override));

  MOCK_METHOD(void,
              SetIPv6Addresses,
              (int interface_index, const std::vector<net_base::IPv6CIDR>&),
              (override));
};
}  // namespace patchpanel

#endif  // PATCHPANEL_NETWORK_MOCK_ADDRESS_SERVICE_H_
