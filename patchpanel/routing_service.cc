// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <iostream>
#include <string_view>

#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>
#include <base/strings/strcat.h>

namespace patchpanel {

// Make sure that the compiler is not doing padding.
static_assert(sizeof(Fwmark) == sizeof(uint32_t));

RoutingService::RoutingService() {}

int RoutingService::GetSockopt(
    int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
  return getsockopt(sockfd, level, optname, optval, optlen);
}

int RoutingService::SetSockopt(
    int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
  return setsockopt(sockfd, level, optname, optval, optlen);
}

bool RoutingService::SetFwmark(int sockfd, Fwmark mark, Fwmark mask) {
  uint32_t fwmark_value = 0;
  socklen_t fwmark_len = sizeof(fwmark_value);
  if (GetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, &fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=" << mark.ToString()
                << " mask=" << mask.ToString()
                << " getsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  fwmark_value = (mark & mask).Value() | (fwmark_value & ~mask.Value());

  fwmark_len = sizeof(fwmark_value);
  if (SetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=" << mark.ToString()
                << " mask=" << mask.ToString()
                << " setsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  return true;
}

bool RoutingService::TagSocket(int sockfd,
                               std::optional<int> network_id,
                               VPNRoutingPolicy vpn_policy) {
  if (vpn_policy == VPNRoutingPolicy::kRouteOnVPN && network_id.has_value()) {
    LOG(ERROR) << __func__
               << ": route_on_vpn policy and network_id should not be set at "
                  "the same time";
    return false;
  }

  // TODO(b/322083502): Do some basic verification that this socket is not
  // connected.

  Fwmark mark = {.fwmark = 0};

  if (network_id.has_value()) {
    // TODO(b/322083502): Assume network_id is interface_id now so that we can
    // test this function. Change this to the real implementation after
    // network_id is ready.
    mark.rt_table_id =
        kInterfaceTableIdIncrement + static_cast<uint16_t>(*network_id);
  }

  switch (vpn_policy) {
    case VPNRoutingPolicy::kDefault:
      break;
    case VPNRoutingPolicy::kRouteOnVPN:
      mark = mark | kFwmarkRouteOnVpn;
      break;
    case VPNRoutingPolicy::kBypassVPN:
      mark = mark | kFwmarkBypassVpn;
      break;
  }

  const auto mask = kFwmarkRoutingMask | kFwmarkVpnMask;
  LOG(INFO) << "SetFwmark mark=" << mark.ToString()
            << " mask=" << mask.ToString();
  return SetFwmark(sockfd, mark, mask);
}

std::string QoSFwmarkWithMask(QoSCategory category) {
  auto mark = Fwmark::FromQoSCategory(category);
  return base::StrCat(
      {mark.ToString(), "/", kFwmarkQoSCategoryMask.ToString()});
}

std::string SourceFwmarkWithMask(TrafficSource source) {
  auto mark = Fwmark::FromSource(source);
  return base::StrCat({mark.ToString(), "/", kFwmarkAllSourcesMask.ToString()});
}

std::string_view TrafficSourceName(TrafficSource source) {
  static constexpr auto kTrafficSourceNames =
      base::MakeFixedFlatMap<TrafficSource, std::string_view>({
          {kArc, "ARC"},
          {kArcVpn, "ARC_VPN"},
          {kBorealisVM, "BOREALIS_VM"},
          {kBruschettaVM, "BRUSCHETTA_VM"},
          {kChrome, "CHROME"},
          {kCrostiniVM, "CROSTINI_VM"},
          {kHostVpn, "HOST_VPN"},
          {kParallelsVM, "PARALLELS_VM"},
          {kSystem, "SYSTEM"},
          {kTetherDownstream, "TETHER_DOWNSTREAM"},
          {kUnknown, "UNKNOWN"},
          {kUpdateEngine, "UPDATE_ENGINE"},
          {kUser, "USER"},
          {kWiFiDirect, "WIFI_DIRECT"},
          {kWiFiLOHS, "WIFI_LOHS"},
      });
  const auto it = kTrafficSourceNames.find(source);
  return it != kTrafficSourceNames.end() ? it->second : "UNKNOWN";
}

std::ostream& operator<<(std::ostream& stream, const LocalSourceSpecs& source) {
  return stream << "{source: " << TrafficSourceName(source.source_type)
                << ", uid: " << (source.uid_name ? source.uid_name : "")
                << ", classid: " << source.classid
                << ", is_on_vpn: " << (source.is_on_vpn ? "true" : "false")
                << "}";
}

}  // namespace patchpanel
