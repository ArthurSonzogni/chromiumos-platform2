// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_MOCK_DHCP_PROXY_H_
#define SHILL_DHCP_MOCK_DHCP_PROXY_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/dhcp/dhcp_proxy_interface.h"

namespace shill {

class MockDHCPProxy : public DHCPProxyInterface {
 public:
  MockDHCPProxy();
  ~MockDHCPProxy() override;

  MOCK_METHOD1(Rebind, void(const std::string& interface));
  MOCK_METHOD1(Release, void(const std::string& interface));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDHCPProxy);
};

}  // namespace shill

#endif  // SHILL_DHCP_MOCK_DHCP_PROXY_H_
