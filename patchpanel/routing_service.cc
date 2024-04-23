// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/strcat.h>

namespace patchpanel {

// Make sure that the compiler is not doing padding.
static_assert(sizeof(Fwmark) == sizeof(uint32_t));

RoutingService::RoutingService(System* system,
                               LifelineFDService* lifeline_fd_service)
    : system_(system), lifeline_fd_svc_(lifeline_fd_service) {}

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

bool RoutingService::TagSocket(
    int sockfd,
    std::optional<int> network_id,
    VPNRoutingPolicy vpn_policy,
    std::optional<TrafficAnnotationId> annotation_id) {
  if (vpn_policy == VPNRoutingPolicy::kRouteOnVPN && network_id.has_value()) {
    LOG(ERROR) << __func__
               << ": route_on_vpn policy and network_id should not be set at "
                  "the same time";
    return false;
  }

  if (annotation_id.has_value()) {
    // TODO(b/331744250): add fwmark to mark the socket as audited.
    return true;
  }

  // TODO(b/322083502): Do some basic verification that this socket is not
  // connected.

  Fwmark mark = {.fwmark = 0};

  if (network_id.has_value()) {
    std::optional<Fwmark> routing_fwmark = GetRoutingFwmark(*network_id);
    if (routing_fwmark) {
      mark.rt_table_id = routing_fwmark->rt_table_id;
    }
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

int RoutingService::AllocateNetworkID() {
  CHECK_LT(0, next_network_id_);
  return next_network_id_++;
}

bool RoutingService::AssignInterfaceToNetwork(int network_id,
                                              std::string_view ifname,
                                              base::ScopedFD client_fd) {
  if (auto it = network_ids_to_interfaces_.find(network_id);
      it != network_ids_to_interfaces_.end()) {
    LOG(ERROR) << __func__ << ": " << it->second << " already assigned to "
               << network_id;
    return false;
  }

  if (auto it = interfaces_to_network_ids_.find(ifname);
      it != interfaces_to_network_ids_.end()) {
    LOG(ERROR) << __func__ << ": " << ifname << " already assigned to "
               << it->second;
    return false;
  }

  base::ScopedClosureRunner cancel_lifeline_fd =
      lifeline_fd_svc_->AddLifelineFD(
          std::move(client_fd),
          base::BindOnce(&RoutingService::ForgetNetworkID,
                         weak_factory_.GetWeakPtr(), network_id));
  if (!cancel_lifeline_fd) {
    LOG(ERROR) << __func__ << ": Failed to create lifeline fd";
    return false;
  }

  LOG(INFO) << __func__ << ": " << network_id << " <-> " << ifname;
  network_ids_to_interfaces_[network_id] = ifname;
  interfaces_to_network_ids_.emplace(ifname, network_id);
  cancel_lifeline_fds_.emplace(network_id, std::move(cancel_lifeline_fd));
  return true;
}

void RoutingService::ForgetNetworkID(int network_id) {
  const auto it = network_ids_to_interfaces_.find(network_id);
  if (it == network_ids_to_interfaces_.end()) {
    LOG(ERROR) << __func__ << ": Unknown " << network_id;
    return;
  }
  LOG(INFO) << __func__ << ": " << network_id << " <-> " << it->second;
  interfaces_to_network_ids_.erase(it->second);
  network_ids_to_interfaces_.erase(it);
  cancel_lifeline_fds_.erase(network_id);
}

const std::string* RoutingService::GetInterface(int network_id) const {
  const auto it = network_ids_to_interfaces_.find(network_id);
  if (it == network_ids_to_interfaces_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::optional<Fwmark> RoutingService::GetRoutingFwmark(int network_id) const {
  const std::string* ifname = GetInterface(network_id);
  if (!ifname) {
    return std::nullopt;
  }
  int ifindex = system_->IfNametoindex(*ifname);
  if (ifindex == 0) {
    return std::nullopt;
  }
  return Fwmark::FromIfIndex(ifindex);
}

std::optional<int> RoutingService::GetNetworkID(std::string_view ifname) const {
  const auto it = interfaces_to_network_ids_.find(ifname);
  if (it == interfaces_to_network_ids_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<int> RoutingService::GetNetworkIDs() const {
  std::vector<int> network_ids;
  network_ids.reserve(network_ids_to_interfaces_.size());
  for (const auto& [network_id, _] : network_ids_to_interfaces_) {
    network_ids.push_back(network_id);
  }
  return network_ids;
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
