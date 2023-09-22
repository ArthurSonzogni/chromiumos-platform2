// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_TYPES_H_
#define SHILL_VPN_VPN_TYPES_H_

#include <optional>
#include <string>
#include <string_view>

namespace shill {

enum class VPNType {
  kARC,
  kIKEv2,
  kL2TPIPsec,
  kOpenVPN,
  kThirdParty,  // Chrome VpnProvider Apps
  kWireGuard,
};

// Maps the VPN type between strings and enums. Note that the strings are
// mainly used in D-Bus interface and profile storage, and we a different set
// of strings in metrics.
std::optional<VPNType> VPNTypeStringToEnum(std::string_view type);
std::string VPNTypeEnumToString(VPNType type);

}  // namespace shill

#endif  // SHILL_VPN_VPN_TYPES_H_
