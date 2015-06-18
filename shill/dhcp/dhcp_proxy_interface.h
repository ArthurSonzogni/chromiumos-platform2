// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_DHCP_PROXY_INTERFACE_H_
#define SHILL_DHCP_DHCP_PROXY_INTERFACE_H_

#include <string>

namespace shill {

// These are the methods that a DHCP proxy must support. The interface is
// provided so that it can be mocked in tests.
class DHCPProxyInterface {
 public:
  virtual ~DHCPProxyInterface() {}

  virtual void Rebind(const std::string& interface) = 0;
  virtual void Release(const std::string& interface) = 0;
};

}  // namespace shill

#endif  // SHILL_DHCP_DHCP_PROXY_INTERFACE_H_
