// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_CONNECTION_UNDER_TEST_H_
#define SHILL_VPN_VPN_CONNECTION_UNDER_TEST_H_

#include <memory>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/network_config.h>

#include "shill/event_dispatcher.h"
#include "shill/service.h"
#include "shill/vpn/vpn_connection.h"

namespace shill {

// A simple VPNConnection implementation which can be used in tests.
class VPNConnectionUnderTest : public VPNConnection {
 public:
  VPNConnectionUnderTest(std::unique_ptr<Callbacks> callbacks,
                         EventDispatcher* dispatcher);

  VPNConnectionUnderTest(const VPNConnectionUnderTest&) = delete;
  VPNConnectionUnderTest& operator=(const VPNConnectionUnderTest&) = delete;

  // Make these two functions public to be accessible by EXPECT_CALL.
  MOCK_METHOD(void, OnConnect, (), (override));
  MOCK_METHOD(void, OnDisconnect, (), (override));

  void TriggerConnected(
      const std::string& link_name,
      int interface_index,
      std::unique_ptr<net_base::NetworkConfig> network_config);
  void TriggerFailure(Service::ConnectFailure reason, std::string_view detail);
  void TriggerStopped();

  void set_state(State state) { state_ = state; }
};

}  // namespace shill

#endif  // SHILL_VPN_VPN_CONNECTION_UNDER_TEST_H_
