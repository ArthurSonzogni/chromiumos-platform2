// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_NETWORK_PROXY_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_NETWORK_PROXY_INTERFACE_H_

#include <map>

#include <dbus-c++/dbus.h>

namespace shill {

// SupplicantNetworkProxyInterface declares only the subset of
// fi::w1::wpa_supplicant1::Network_proxy that is actually used by WiFi.
class SupplicantNetworkProxyInterface {
 public:
  virtual ~SupplicantNetworkProxyInterface() {}

  virtual void SetEnabled(bool enabled) = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_NETWORK_PROXY_INTERFACE_H_
