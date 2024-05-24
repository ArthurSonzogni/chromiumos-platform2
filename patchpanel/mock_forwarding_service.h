// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_FORWARDING_SERVICE_H_
#define PATCHPANEL_MOCK_FORWARDING_SERVICE_H_

#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "patchpanel/forwarding_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class MockForwardingService : public ForwardingService {
 public:
  MockForwardingService() : ForwardingService() {}
  MockForwardingService(const MockForwardingService&) = delete;
  MockForwardingService& operator=(const MockForwardingService&) = delete;
  virtual ~MockForwardingService() = default;

  MOCK_METHOD(void,
              StartIPv6NDPForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual,
               std::optional<int> mtu,
               std::optional<int> hop_limit),
              (override));
  MOCK_METHOD(void,
              StartBroadcastForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual),
              (override));
  MOCK_METHOD(void,
              StartMulticastForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual),
              (override));
  MOCK_METHOD(void,
              StopIPv6NDPForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual),
              (override));
  MOCK_METHOD(void,
              StopBroadcastForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual),
              (override));
  MOCK_METHOD(void,
              StopMulticastForwarding,
              (const ShillClient::Device& shill_device,
               const std::string& ifname_virtual),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_FORWARDING_SERVICE_H_
