// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_MOCK_VPN_DRIVER_H_
#define SHILL_VPN_MOCK_VPN_DRIVER_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/vpn/vpn_driver.h"

namespace shill {

class MockVPNDriver : public VPNDriver {
 public:
  MockVPNDriver();
  ~MockVPNDriver() override;

  MOCK_METHOD(bool, ClaimInterface, (const std::string&, int), (override));
  MOCK_METHOD(void, Connect, (const VPNServiceRefPtr&, Error*), (override));
  MOCK_METHOD(void, Disconnect, (), (override));
  MOCK_METHOD(bool, Load, (StoreInterface*, const std::string&), (override));
  MOCK_METHOD(bool,
              Save,
              (StoreInterface*, const std::string&, bool),
              (override));
  MOCK_METHOD(void, UnloadCredentials, (), (override));
  MOCK_METHOD(void, InitPropertyStore, (PropertyStore*), (override));
  MOCK_METHOD(std::string, GetProviderType, (), (const, override));
  MOCK_METHOD(std::string, GetHost, (), (const, override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockVPNDriver);
};

}  // namespace shill

#endif  // SHILL_VPN_MOCK_VPN_DRIVER_H_
