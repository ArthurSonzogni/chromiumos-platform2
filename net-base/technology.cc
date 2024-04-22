// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/technology.h"

#include <optional>
#include <string_view>

#include <base/containers/fixed_flat_map.h>

namespace net_base {
namespace {
constexpr std::string_view kCellularString = "cellular";
constexpr std::string_view kEthernetString = "ethernet";
constexpr std::string_view kVPNString = "vpn";
constexpr std::string_view kWiFiString = "wifi";
constexpr std::string_view kWiFiDirectString = "wifi_direct";
}  // namespace

std::string_view ToString(Technology technology) {
  switch (technology) {
    case Technology::kCellular:
      return kCellularString;
    case Technology::kEthernet:
      return kEthernetString;
    case Technology::kVPN:
      return kVPNString;
    case Technology::kWiFi:
      return kWiFiString;
    case Technology::kWiFiDirect:
      return kWiFiDirectString;
  }
}

std::optional<Technology> FromString(std::string_view str) {
  static constexpr auto kStringToTechnology =
      base::MakeFixedFlatMap<std::string_view, Technology>({
          {kCellularString, Technology::kCellular},
          {kEthernetString, Technology::kEthernet},
          {kVPNString, Technology::kVPN},
          {kWiFiString, Technology::kWiFi},
          {kWiFiDirectString, Technology::kWiFiDirect},
      });

  const auto iter = kStringToTechnology.find(str);
  if (iter == kStringToTechnology.end()) {
    return std::nullopt;
  }
  return iter->second;
}

std::ostream& operator<<(std::ostream& os, Technology technology) {
  os << ToString(technology);
  return os;
}

}  // namespace net_base
