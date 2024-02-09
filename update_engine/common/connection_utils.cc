// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/connection_utils.h"

#include <shill/dbus-constants.h>

namespace {
// Not defined by shill since we don't use this outside of UE.
constexpr char kTypeDisconnected[] = "Disconnected";
constexpr char kTypeUnknown[] = "Unknown";
}  // namespace

namespace chromeos_update_engine {
namespace connection_utils {

ConnectionType ParseConnectionType(const std::string& type_str) {
  if (type_str == shill::kTypeEthernet) {
    return ConnectionType::kEthernet;
  } else if (type_str == shill::kTypeWifi) {
    return ConnectionType::kWifi;
  } else if (type_str == shill::kTypeCellular) {
    return ConnectionType::kCellular;
  } else if (type_str == kTypeDisconnected) {
    return ConnectionType::kDisconnected;
  }
  return ConnectionType::kUnknown;
}

const char* StringForConnectionType(ConnectionType type) {
  switch (type) {
    case ConnectionType::kEthernet:
      return shill::kTypeEthernet;
    case ConnectionType::kWifi:
      return shill::kTypeWifi;
    case ConnectionType::kCellular:
      return shill::kTypeCellular;
    case ConnectionType::kDisconnected:
      return kTypeDisconnected;
    case ConnectionType::kUnknown:
      return kTypeUnknown;
  }
  return kTypeUnknown;
}

}  // namespace connection_utils

}  // namespace chromeos_update_engine
