// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_GROUP_PROXY_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_GROUP_PROXY_INTERFACE_H_

#include <string>
#include <vector>

#include "supplicant/dbus-proxies.h"

namespace shill {

// SupplicantGroupProxyInterface declares only the subset of
// fi::w1::wpa_supplicant1::Group_proxy that is actually used by WiFi P2P.
class SupplicantGroupProxyInterface {
 public:
  virtual ~SupplicantGroupProxyInterface() = default;
  virtual bool GetMembers(std::vector<dbus::ObjectPath>* members) const = 0;
  virtual bool GetRole(std::string* role) const = 0;
  virtual bool GetSSID(std::vector<uint8_t>* ssid) const = 0;
  virtual bool GetBSSID(std::vector<uint8_t>* bssid) const = 0;
  virtual bool GetFrequency(uint16_t* frequency) const = 0;
  virtual bool GetPassphrase(std::string* passphrase) const = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_GROUP_PROXY_INTERFACE_H_
