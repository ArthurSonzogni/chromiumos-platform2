// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/shill_utils.h"

#include <dbus/shill/dbus-constants.h>

namespace minios {

std::string ToString(WifiStationType station) {
  switch (station) {
    case WifiStationType::MANAGED:
      return shill::kModeManaged;
    case WifiStationType::ADHOC:
      return "adhoc";
    default:
      return shill::kUnknownString;
  }
}

std::string ToString(WifiSecurityType security) {
  switch (security) {
    case WifiSecurityType::NONE:
      return shill::kSecurityNone;
    case WifiSecurityType::PSK:
      return shill::kSecurityPsk;
    default:
      return shill::kUnknownString;
  }
}

std::string ToString(WifiTechnologyType technology) {
  switch (technology) {
    case WifiTechnologyType::WIFI:
      return shill::kTypeWifi;
    case WifiTechnologyType::CELLULAR:
      return shill::kTypeCellular;
    default:
      return shill::kUnknownString;
  }
}

}  // namespace minios
