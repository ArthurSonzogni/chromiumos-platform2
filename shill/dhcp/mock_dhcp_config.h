// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_MOCK_DHCP_CONFIG_H_
#define SHILL_DHCP_MOCK_DHCP_CONFIG_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/dhcp/dhcp_config.h"

namespace shill {

class MockDHCPConfig : public DHCPConfig {
 public:
  MockDHCPConfig(ControlInterface *control_interface,
                 const std::string &device_name);
  ~MockDHCPConfig() override;

  void ProcessEventSignal(const std::string &reason,
                          const Configuration &configuration) override;
  void ProcessStatusChangeSignal(const std::string &status) override;

  MOCK_METHOD0(RequestIP, bool());
  MOCK_METHOD1(ReleaseIP, bool(ReleaseReason));
  MOCK_METHOD0(RenewIP, bool());
  MOCK_METHOD1(set_minimum_mtu, void(int));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDHCPConfig);
};

}  // namespace shill

#endif  // SHILL_DHCP_MOCK_DHCP_CONFIG_H_
