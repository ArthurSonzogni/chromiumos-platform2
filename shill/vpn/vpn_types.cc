// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_types.h"

#include <string>
#include <string_view>

#include <base/containers/fixed_flat_map.h>
#include <base/notreached.h>
#include <chromeos/dbus/shill/dbus-constants.h>

namespace shill {

std::optional<VPNType> VPNTypeStringToEnum(std::string_view type) {
  static constexpr auto dict =
      base::MakeFixedFlatMap<std::string_view, VPNType>({
          {kProviderArcVpn, VPNType::kARC},
          {kProviderIKEv2, VPNType::kIKEv2},
          {kProviderL2tpIpsec, VPNType::kL2TPIPsec},
          {kProviderOpenVpn, VPNType::kOpenVPN},
          {kProviderThirdPartyVpn, VPNType::kThirdParty},
          {kProviderWireGuard, VPNType::kWireGuard},
      });
  const auto it = dict.find(type);
  if (it == dict.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string VPNTypeEnumToString(VPNType type) {
  switch (type) {
    case VPNType::kARC:
      return kProviderArcVpn;
    case VPNType::kIKEv2:
      return kProviderIKEv2;
    case VPNType::kL2TPIPsec:
      return kProviderL2tpIpsec;
    case VPNType::kOpenVPN:
      return kProviderOpenVpn;
    case VPNType::kThirdParty:
      return kProviderThirdPartyVpn;
    case VPNType::kWireGuard:
      return kProviderWireGuard;
  }
  NOTREACHED();
  return "";
}

}  // namespace shill
