// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_MOCK_OPENVPN_DRIVER_H_
#define SHILL_VPN_MOCK_OPENVPN_DRIVER_H_

#include <string>
#include <string_view>

#include <gmock/gmock.h>

#include "shill/vpn/openvpn_driver.h"

namespace shill {

class MockOpenVPNDriver : public OpenVPNDriver {
 public:
  MockOpenVPNDriver(Manager* manager, ProcessManager* process_manager);
  MockOpenVPNDriver(const MockOpenVPNDriver&) = delete;
  MockOpenVPNDriver& operator=(const MockOpenVPNDriver&) = delete;

  ~MockOpenVPNDriver() override;

  MOCK_METHOD(void, OnReconnecting, (ReconnectReason), (override));
  MOCK_METHOD(void,
              FailService,
              (Service::ConnectFailure, std::string_view),
              (override));
  MOCK_METHOD(void, ReportCipherMetrics, (std::string_view), (override));
};

}  // namespace shill

#endif  // SHILL_VPN_MOCK_OPENVPN_DRIVER_H_
