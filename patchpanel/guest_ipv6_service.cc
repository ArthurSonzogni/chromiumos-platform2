// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/guest_ipv6_service.h"

#include <net/ethernet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signal.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notimplemented.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/technology.h>

#include "patchpanel/ndproxy.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

constexpr char kRadvdRunDir[] = "/run/radvd";
constexpr char kRadvdPath[] = "/usr/sbin/radvd";
constexpr char kRadvdConfigFilePrefix[] = "radvd.conf.";
constexpr char kRadvdPidFilePrefix[] = "radvd.pid.";
constexpr base::TimeDelta kTimeoutForSIGTERM = base::Seconds(2);
constexpr base::TimeDelta kTimeoutForSIGKILL = base::Seconds(1);

// b/360132462: To bypass the filter on certain mobile carriers that block
// tethering traffic, set RA server to advertise 65 as CurrentHopLimit, and
// ndproxy to increase CurrentHopLimit value by 1 when proxying RA, so that
// packets from guest have a Hop Limit value of  64 when sent outbound. Note
// that the actual tethering traffic should not use this value and should
// specific one during StartForwarding().
constexpr int kDefaultDownstreamHopLimit =
    NDProxy::kIncreaseCurHopLimit ? 65 : 64;

// Helper struct local to this file for logging an uplink / downlink interface
// pair.
struct LinkPair {
  std::string_view uplink;
  std::string_view downlink;
};

std::ostream& operator<<(std::ostream& stream, const LinkPair& pair) {
  return stream << "uplink: " << pair.uplink << ", downlink: " << pair.downlink;
}

}  // namespace

// TODO(b/228585272): Support prefix larger than /64
// static
net_base::IPv6CIDR GuestIPv6Service::IPAddressTo64BitPrefix(
    const net_base::IPv6Address& addr) {
  const int prefix_length = 64;
  return net_base::IPv6CIDR::CreateFromAddressAndPrefix(addr, prefix_length)
      ->GetPrefixCIDR();
}

GuestIPv6Service::GuestIPv6Service(SubprocessControllerInterface* nd_proxy,
                                   Datapath* datapath,
                                   System* system)
    : nd_proxy_(nd_proxy), datapath_(datapath), system_(system) {
  nd_proxy_->RegisterFeedbackMessageHandler(base::BindRepeating(
      &GuestIPv6Service::OnNDProxyMessage, weak_factory_.GetWeakPtr()));
  nd_proxy_->Listen();
}

GuestIPv6Service::~GuestIPv6Service() = default;

void GuestIPv6Service::StartForwarding(
    const ShillClient::Device& upstream_shill_device,
    const std::string& ifname_downlink,
    const std::optional<int>& mtu,
    const std::optional<int>& hop_limit,
    bool downlink_is_tethering) {
  const std::string& ifname_uplink = upstream_shill_device.ifname;
  const LinkPair pair = {.uplink = ifname_uplink, .downlink = ifname_downlink};
  LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": "
            << pair << ", mtu: " << (mtu ? std::to_string(*mtu) : "default")
            << ", hop_limit: "
            << (hop_limit ? std::to_string(*hop_limit) : "default");
  int if_id_uplink = system_->IfNametoindex(ifname_uplink);
  if (if_id_uplink == 0) {
    PLOG(ERROR) << upstream_shill_device.logging_tag << " " << __func__
                << ": Get interface index failed on " << ifname_uplink;
    return;
  }
  if_cache_[ifname_uplink] = if_id_uplink;
  int if_id_downlink = system_->IfNametoindex(ifname_downlink);
  if (if_id_downlink == 0) {
    PLOG(ERROR) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                << pair << ", Get interface index failed on "
                << ifname_downlink;
    return;
  }
  if_cache_[ifname_downlink] = if_id_downlink;

  // Lookup ForwardEntry for the specified uplink. If it does not exist, create
  // a new one based on its device type.
  ForwardMethod forward_method;
  if (forward_record_.find(ifname_uplink) != forward_record_.end()) {
    forward_method = forward_record_[ifname_uplink].method;
    forward_record_[ifname_uplink].downstream_ifnames.insert(ifname_downlink);
  } else {
    forward_method = GetForwardMethod(upstream_shill_device);
    if (forward_method == ForwardMethod::kMethodUnknown) {
      LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                << pair << ", IPv6 forwarding not supported on device type";
      return;
    }
    forward_record_[ifname_uplink] = {
        forward_method, {ifname_downlink}, std::nullopt, std::nullopt};
  }

  // Set the MTU and CurHopLimit value to |forward_record_|.
  if (mtu && forward_record_[ifname_uplink].mtu != *mtu) {
    forward_record_[ifname_uplink].mtu = *mtu;
  }
  if (hop_limit && forward_record_[ifname_uplink].hop_limit != *hop_limit) {
    forward_record_[ifname_uplink].hop_limit = *hop_limit;
  }

  if (!datapath_->MaskInterfaceFlags(ifname_uplink, IFF_ALLMULTI)) {
    LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                 << pair << ", Failed to setup all multicast mode";
  }
  if (!datapath_->MaskInterfaceFlags(ifname_downlink, IFF_ALLMULTI)) {
    LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                 << pair << ", Failed to setup all multicast mode";
  }

  switch (forward_method) {
    case ForwardMethod::kMethodNDProxy:
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA_RS_RA, if_id_uplink,
                         if_id_downlink);
      break;
    case ForwardMethod::kMethodNDProxyInjectingRA:
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS,
          if_id_uplink, if_id_downlink);
      break;
    case ForwardMethod::kMethodRAServer:
      // No need of proxying between downlink and uplink for RA server.
      SendNDProxyControl(NDProxyControlMessage::START_NEIGHBOR_MONITOR,
                         if_id_downlink, 0);
      break;
    case ForwardMethod::kMethodUnknown:
      NOTREACHED_IN_MIGRATION();
  }

  // Start NA proxying between the new downlink and existing downlinks, if any.
  CHECK(forward_record_.find(ifname_uplink) != forward_record_.end());
  for (const auto& another_downlink :
       forward_record_[ifname_uplink].downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      int32_t if_id_downlink2 = if_cache_[another_downlink];
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA, if_id_downlink,
                         if_id_downlink2);
    }
  }

  if (upstream_shill_device.network_config.ipv6_addresses.empty()) {
    uplink_ips_.erase(ifname_uplink);
    return;
  }
  // GuestIPv6Service only uses the first IPv6 address on uplink currently.
  const auto uplink_ip =
      upstream_shill_device.network_config.ipv6_addresses[0].address();
  uplink_ips_[ifname_uplink] = uplink_ip;

  // Allow IPv6 address on uplink to be resolvable on the downlink
  if (!datapath_->AddIPv6NeighborProxy(ifname_downlink, uplink_ip)) {
    LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                 << pair << ", Failed to setup the IPv6 neighbor {" << uplink_ip
                 << "} proxy on dev " << ifname_downlink;
  }

  if (forward_method == ForwardMethod::kMethodRAServer) {
    if (!StartRAServer(ifname_downlink, IPAddressTo64BitPrefix(uplink_ip),
                       uplink_dns_[ifname_uplink],
                       forward_record_[ifname_uplink].mtu,
                       forward_record_[ifname_uplink].hop_limit)) {
      LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__
                   << ": " << pair
                   << ", Failed to start RA server on downlink with uplink IP {"
                   << uplink_ip << "}";
    }
  }
}

void GuestIPv6Service::StopForwarding(
    const ShillClient::Device& upstream_shill_device,
    const std::string& ifname_downlink) {
  const std::string& ifname_uplink = upstream_shill_device.ifname;
  const LinkPair pair = {.uplink = ifname_uplink, .downlink = ifname_downlink};
  LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": "
            << pair;

  const auto it = forward_record_.find(ifname_uplink);
  if (it == forward_record_.end()) {
    return;
  }

  auto& forward_record = it->second;
  if (forward_record.downstream_ifnames.find(ifname_downlink) ==
      forward_record.downstream_ifnames.end()) {
    return;
  }

  if (forward_record.method != ForwardMethod::kMethodRAServer) {
    SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                       if_cache_[ifname_uplink], if_cache_[ifname_downlink]);
  }

  // Remove proxying between specified downlink and all other downlinks in the
  // same group.
  for (const auto& another_downlink : forward_record.downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[ifname_downlink],
                         if_cache_[another_downlink]);
    }
  }

  // Remove ip neigh proxy entry
  const auto uplink_ip = GetUplinkIp(ifname_uplink);
  if (uplink_ip) {
    datapath_->RemoveIPv6NeighborProxy(ifname_downlink, *uplink_ip);
  }
  // Remove downlink /128 routes
  for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
    datapath_->RemoveIPv6HostRoute(
        *net_base::IPv6CIDR::CreateFromAddressAndPrefix(neighbor_ip, 128));
    // Remove offloaded downlink ip neigh proxy entry for ARC sleep mode
    if (arc_filter_ifnames_.contains(ifname_downlink)) {
      datapath_->RemoveIPv6NeighborProxy(ifname_uplink, neighbor_ip);
    }
  }
  downstream_neighbors_[ifname_downlink].clear();

  if (forward_record.method == ForwardMethod::kMethodRAServer) {
    SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR,
                       if_cache_[ifname_downlink], 0);
    if (uplink_ip) {
      StopRAServer(ifname_downlink);
    }
  }

  forward_record.downstream_ifnames.erase(ifname_downlink);
  if (forward_record.downstream_ifnames.empty()) {
    forward_record_.erase(it);
  }
}

void GuestIPv6Service::StopUplink(
    const ShillClient::Device& upstream_shill_device) {
  const std::string& ifname_uplink = upstream_shill_device.ifname;
  LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__;

  if (forward_record_.find(ifname_uplink) == forward_record_.end()) {
    return;
  }

  // Remove proxying between specified uplink and all downlinks.
  if (forward_record_[ifname_uplink].method != ForwardMethod::kMethodRAServer) {
    for (const auto& ifname_downlink :
         forward_record_[ifname_uplink].downstream_ifnames) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[ifname_uplink], if_cache_[ifname_downlink]);
    }
  }

  // Remove proxying between all downlink pairs in the forward group.
  const auto& downlinks = forward_record_[ifname_uplink].downstream_ifnames;
  for (auto it1 = downlinks.begin(); it1 != downlinks.end(); it1++) {
    for (auto it2 = std::next(it1); it2 != downlinks.end(); it2++) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[it1->c_str()], if_cache_[it2->c_str()]);
    }
  }

  const auto uplink_ip = GetUplinkIp(ifname_uplink);
  for (const auto& ifname_downlink :
       forward_record_[ifname_uplink].downstream_ifnames) {
    // Remove ip neigh proxy entry
    if (uplink_ip) {
      datapath_->RemoveIPv6NeighborProxy(ifname_downlink, *uplink_ip);
    }
    // Remove downlink /128 routes
    for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
      datapath_->RemoveIPv6HostRoute(
          *net_base::IPv6CIDR::CreateFromAddressAndPrefix(neighbor_ip, 128));
      // Remove offloaded downlink ip neigh proxy entry for ARC sleep mode
      if (arc_filter_ifnames_.contains(ifname_downlink)) {
        datapath_->RemoveIPv6NeighborProxy(ifname_uplink, neighbor_ip);
      }
    }
    downstream_neighbors_[ifname_downlink].clear();
  }

  if (forward_record_[ifname_uplink].method == ForwardMethod::kMethodRAServer) {
    for (const auto& ifname_downlink :
         forward_record_[ifname_uplink].downstream_ifnames) {
      SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR,
                         if_cache_[ifname_downlink], 0);
      if (uplink_ip) {
        StopRAServer(ifname_downlink);
      }
    }
  }

  forward_record_.erase(ifname_uplink);
}

void GuestIPv6Service::OnUplinkIPv6Changed(
    const ShillClient::Device& upstream_shill_device) {
  const std::string& ifname = upstream_shill_device.ifname;
  const auto old_uplink_ip = GetUplinkIp(ifname);

  if (upstream_shill_device.network_config.ipv6_addresses.empty()) {
    LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": {"
              << ((old_uplink_ip) ? old_uplink_ip->ToString() : "")
              << "} to {}";
    uplink_ips_.erase(ifname);
    if (!old_uplink_ip ||
        forward_record_.find(ifname) == forward_record_.end()) {
      return;
    }
    for (const auto& ifname_downlink :
         forward_record_[ifname].downstream_ifnames) {
      // Remove ip neigh proxy entry
      datapath_->RemoveIPv6NeighborProxy(ifname_downlink, *old_uplink_ip);
      // Remove downlink /128 routes
      for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
        datapath_->RemoveIPv6HostRoute(
            *net_base::IPv6CIDR::CreateFromAddressAndPrefix(neighbor_ip, 128));
      }
      // Stop RA servers
      if (forward_record_[ifname].method == ForwardMethod::kMethodRAServer) {
        StopRAServer(ifname_downlink);
      }
      // b/304887221: Note that we do not clear downstream_neighbors_ cache here
      // in order to more gracefully handle the case that ChromeOS host loses
      // uplink IP and comes back soon within the same network.
    }
    return;
  }

  // GuestIPv6Service only uses the first IPv6 address on uplink currently.
  const auto new_uplink_ip =
      upstream_shill_device.network_config.ipv6_addresses[0].address();
  LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": {"
            << ((old_uplink_ip) ? old_uplink_ip->ToString() : "") << "} to {"
            << new_uplink_ip << "}";
  if (old_uplink_ip == new_uplink_ip) {
    return;
  }
  uplink_ips_[ifname] = new_uplink_ip;

  const auto current_record = forward_record_.find(ifname);
  if (current_record == forward_record_.end()) {
    return;
  }

  const auto new_method = GetForwardMethod(upstream_shill_device);
  if (current_record->second.method != new_method) {
    // Need a copy here since StopUplink() will modify the record.
    auto downlinks = current_record->second.downstream_ifnames;
    auto mtu = current_record->second.mtu;
    auto hop_limit = current_record->second.hop_limit;

    // StopUplink and StartForwarding will modify |method| so don't need to set
    // it explicitly.
    StopUplink(upstream_shill_device);
    for (const auto& downlink : downlinks) {
      StartForwarding(upstream_shill_device, downlink, mtu, hop_limit);
    }
    return;
  }

  // Note that the order of StartForwarding() and OnUplinkIPv6Changed() is not
  // certain so the `ip neigh proxy` and /128 route changes need to be handled
  // in both code paths. When an uplink is newly connected to,
  // StartForwarding() get called first and then we received
  // OnUplinkIPv6Changed() when uplink get an IPv6 address. When default
  // network switches to an existing uplink, StartForwarding() is after
  // OnUplinkIPv6Changed() (which was already called when it was not default
  // yet).
  for (const auto& ifname_downlink :
       current_record->second.downstream_ifnames) {
    const LinkPair pair = {.uplink = ifname, .downlink = ifname_downlink};
    // Update ip neigh proxy entries
    if (old_uplink_ip) {
      datapath_->RemoveIPv6NeighborProxy(ifname_downlink, *old_uplink_ip);
    }
    if (!datapath_->AddIPv6NeighborProxy(ifname_downlink, new_uplink_ip)) {
      LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__
                   << ": " << pair << ", Failed to setup the IPv6 neighbor {"
                   << new_uplink_ip << "} proxy on dev " << ifname_downlink;
    }

    // Update downlink /128 routes source IP. Note AddIPv6HostRoute uses `ip
    // route replace` so we don't need to remove the old one first.
    auto& neighbor_ips = downstream_neighbors_[ifname_downlink];
    for (auto iter = neighbor_ips.begin(); iter != neighbor_ips.end();) {
      // Skip and remove downstream neighbor IP cache if it is not in the same
      // prefix with the new uplink IP.
      if (!upstream_shill_device.network_config.ipv6_addresses[0]
               .InSameSubnetWith(*iter)) {
        LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__
                  << ": " << pair
                  << ", removing cached downstream neighbor IP {" << *iter
                  << "} because it's not in the subnet of new uplink IP {"
                  << new_uplink_ip << "}";
        iter = neighbor_ips.erase(iter);
        continue;
      }
      LOG(INFO) << upstream_shill_device.logging_tag << " " << __func__ << ": "
                << pair
                << ", update /128 downlink route for existing neighbor IP {"
                << *iter << "} because of new uplink IP {" << new_uplink_ip
                << "}";
      if (!datapath_->AddIPv6HostRoute(
              ifname_downlink,
              *net_base::IPv6CIDR::CreateFromAddressAndPrefix(*iter, 128),
              new_uplink_ip)) {
        LOG(WARNING) << upstream_shill_device.logging_tag << " " << __func__
                     << ": " << pair << ": Failed to setup the IPv6 route {"
                     << *iter << "} dev " << ifname << " src {" << new_uplink_ip
                     << "}";
      }
      ++iter;
    }

    if (current_record->second.method == ForwardMethod::kMethodRAServer) {
      const auto new_prefix = IPAddressTo64BitPrefix(new_uplink_ip);
      if (old_uplink_ip) {
        if (IPAddressTo64BitPrefix(*old_uplink_ip) == new_prefix) {
          continue;
        }
        StopRAServer(ifname_downlink);
      }
      if (!StartRAServer(ifname_downlink, new_prefix, uplink_dns_[ifname],
                         current_record->second.mtu,
                         current_record->second.hop_limit)) {
        LOG(WARNING)
            << upstream_shill_device.logging_tag << " " << __func__ << ": "
            << pair
            << ", Failed to start RA server on downlink with uplink IP {"
            << new_uplink_ip << "}";
      }
    }
  }
}

void GuestIPv6Service::UpdateUplinkIPv6DNS(
    const ShillClient::Device& upstream_shill_device) {
  const std::string& ifname = upstream_shill_device.ifname;
  const auto& old_dns = uplink_dns_[ifname];
  // TODO(b/340974631): Use net_base::IPAddress for DNS caches.
  std::vector<std::string> new_dns;
  for (const auto& dns : upstream_shill_device.network_config.dns_servers) {
    if (dns.GetFamily() == net_base::IPFamily::kIPv6) {
      new_dns.push_back(dns.ToString());
    }
  }
  std::sort(new_dns.begin(), new_dns.end());

  VLOG(1) << __func__ << ": " << ifname << ", {"
          << base::JoinString(old_dns, ",") << "} to {"
          << base::JoinString(new_dns, ",") << "}";
  if (old_dns == new_dns) {
    return;
  }

  if (auto it = forward_record_.find(ifname);
      it != forward_record_.end() &&
      it->second.method == ForwardMethod::kMethodRAServer) {
    for (const auto& ifname_downlink : it->second.downstream_ifnames) {
      const auto uplink_ip = GetUplinkIp(ifname);
      if (uplink_ip) {
        const auto prefix = IPAddressTo64BitPrefix(*uplink_ip);
        StopRAServer(ifname_downlink);
        if (!StartRAServer(ifname_downlink, prefix, new_dns, it->second.mtu,
                           it->second.hop_limit)) {
          const LinkPair pair = {.uplink = ifname, .downlink = ifname_downlink};
          LOG(WARNING)
              << upstream_shill_device.logging_tag << " " << __func__ << ": "
              << pair
              << ", Failed to start RA server on downlink with uplink IP {"
              << *uplink_ip << "}";
        }
      }
    }
  }
  uplink_dns_[ifname] = new_dns;
}

void GuestIPv6Service::StartARCPacketFilter(
    const std::vector<std::string_view>& arc_ifnames) {
  if (!arc_filter_ifnames_.empty()) {
    LOG(WARNING) << __func__ << ": already started.";
    return;
  }
  for (auto item : arc_ifnames) {
    std::string arc_ifname(item);
    auto uplink_ifname = DownlinkToUplink(arc_ifname);
    if (!uplink_ifname) {
      LOG(WARNING) << __func__ << ": " << arc_ifname
                   << " is not an ifname currently being forwarded, skipping.";
      continue;
    }

    LOG(INFO) << __func__ << ": " << arc_ifname;
    arc_filter_ifnames_.insert(arc_ifname);

    // Adds a `neighbor proxy` entry to uplink for every downlink global address
    // that we already know. These need to be removed at `StopARCPacketFilter`,
    // `StopForwarding`, or `StopUplink`. Note that we are not handling new
    // downlinks added at `StartForwarding` as we expect adding a new physical
    // interface will wake up ARC. We are also not adding new proxy entries in
    // `RegisterDownstreamNeighborIP` because we don't expect ARC to configure
    // new address during sleep mode.
    if (downstream_neighbors_.contains(arc_ifname)) {
      for (const auto& downlink_ip : downstream_neighbors_.at(arc_ifname)) {
        if (!datapath_->AddIPv6NeighborProxy(*uplink_ifname, downlink_ip)) {
          LOG(WARNING) << __func__ << ": AddIPv6NeighborProxy on "
                       << *uplink_ifname << " to " << downlink_ip << "failed.";
        }
      }
    }
    int32_t if_id = if_cache_.at(arc_ifname);
    SendNDProxyControl(NDProxyControlMessage::START_NS_NA_FILTER, if_id, 0);
  }
}

void GuestIPv6Service::StopARCPacketFilter() {
  for (auto arc_ifname : arc_filter_ifnames_) {
    auto uplink_ifname = DownlinkToUplink(arc_ifname);
    if (!uplink_ifname) {
      continue;
    }

    LOG(INFO) << __func__ << ": " << arc_ifname;
    if (downstream_neighbors_.contains(arc_ifname)) {
      for (const auto& downlink_ip : downstream_neighbors_.at(arc_ifname)) {
        datapath_->RemoveIPv6NeighborProxy(*uplink_ifname, downlink_ip);
      }
    }
    int32_t if_id = if_cache_.at(arc_ifname);
    SendNDProxyControl(NDProxyControlMessage::STOP_NS_NA_FILTER, if_id, 0);
  }
  arc_filter_ifnames_.clear();
}

void GuestIPv6Service::StartLocalHotspot(
    const std::string& ifname_hotspot_link,
    const std::string& prefix,
    const std::vector<std::string>& rdnss,
    const std::vector<std::string>& dnssl) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::StopLocalHotspot(
    const std::string& ifname_hotspot_link) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::SetForwardMethod(
    const ShillClient::Device& upstream_shill_device, ForwardMethod method) {
  forward_method_override_[upstream_shill_device.ifname] = method;

  const auto it = forward_record_.find(upstream_shill_device.ifname);
  if (it != forward_record_.end()) {
    // Need a copy here since StopUplink() will modify the record
    auto downlinks = it->second.downstream_ifnames;
    auto mtu = it->second.mtu;
    auto hop_limit = it->second.hop_limit;

    StopUplink(upstream_shill_device);
    for (const auto& downlink : downlinks) {
      StartForwarding(upstream_shill_device, downlink, mtu, hop_limit);
    }
  }
}

GuestIPv6Service::ForwardMethod GuestIPv6Service::GetForwardMethod(
    const ShillClient::Device& upstream_device) const {
  const auto ifname_uplink = upstream_device.ifname;
  if (forward_method_override_.find(ifname_uplink) !=
      forward_method_override_.end()) {
    return forward_method_override_.at(ifname_uplink);
  }
  if (!upstream_device.network_config.ipv6_delegated_prefixes.empty()) {
    return GuestIPv6Service::ForwardMethod::kMethodRAServer;
  }
  std::optional<net_base::Technology> type = upstream_device.technology;
  if (!type.has_value()) {
    return GuestIPv6Service::ForwardMethod::kMethodUnknown;
  }
  switch (*type) {
    case net_base::Technology::kEthernet:
    case net_base::Technology::kWiFi:
      return GuestIPv6Service::ForwardMethod::kMethodNDProxy;

    case net_base::Technology::kCellular:
      return GuestIPv6Service::ForwardMethod::kMethodRAServer;

    case net_base::Technology::kWiFiDirect:
    case net_base::Technology::kVPN:
      return GuestIPv6Service::ForwardMethod::kMethodUnknown;
  }
}

void GuestIPv6Service::SendNDProxyControl(
    NDProxyControlMessage::NDProxyRequestType type,
    int32_t if_id_primary,
    int32_t if_id_secondary) {
  VLOG(4) << __func__ << ": Sending " << type << ": " << if_id_primary << "<->"
          << if_id_secondary;
  NDProxyControlMessage msg;
  msg.set_type(type);
  msg.set_if_id_primary(if_id_primary);
  msg.set_if_id_secondary(if_id_secondary);
  ControlMessage cm;
  *cm.mutable_ndproxy_control() = msg;
  nd_proxy_->SendControlMessage(cm);
}

void GuestIPv6Service::OnNDProxyMessage(const FeedbackMessage& fm) {
  if (!fm.has_ndproxy_signal()) {
    LOG(ERROR) << __func__ << ": Unexpected feedback message type";
    return;
  }

  const NDProxySignalMessage& msg = fm.ndproxy_signal();
  if (msg.has_neighbor_detected_signal()) {
    const auto& inner_msg = msg.neighbor_detected_signal();
    const auto ip = net_base::IPv6Address::CreateFromBytes(inner_msg.ip());
    if (!ip) {
      LOG(ERROR)
          << __func__
          << ": Failed to create IPv6Address from NeighborDetectedSignal,"
          << " size=" << inner_msg.ip().size() << " instead of "
          << net_base::IPv6Address::kAddressLength;
      return;
    }
    std::string ifname = system_->IfIndextoname(inner_msg.if_id());
    RegisterDownstreamNeighborIP(ifname, *ip);
    return;
  }

  if (msg.has_router_detected_signal()) {
    // This event is currently not used.
    return;
  }

  LOG(ERROR) << __func__ << ": Unknown NDProxy event ";
  NOTREACHED_IN_MIGRATION();
}

void GuestIPv6Service::RegisterDownstreamNeighborIP(
    const std::string& ifname_downlink, const net_base::IPv6Address& ip) {
  if (downstream_neighbors_[ifname_downlink].count(ip) != 0) {
    return;
  }
  downstream_neighbors_[ifname_downlink].insert(ip);

  const auto& uplink = DownlinkToUplink(ifname_downlink);
  if (!uplink) {
    LOG(INFO) << __func__ << ": downlink: " << ifname_downlink
              << ", neighbor IP {" << ip
              << "}, no corresponding uplink IP yet, postponing /128 route";
    return;
  }

  const LinkPair pair = {.uplink = uplink.value(), .downlink = ifname_downlink};
  const auto uplink_ip = GetUplinkIp(uplink.value());
  const std::string uplink_ip_str = uplink_ip ? uplink_ip->ToString() : "";
  LOG(INFO) << __func__ << ": " << pair << ", neighbor IP {" << ip
            << "}, uplink IP {" << uplink_ip_str << "}";
  if (!datapath_->AddIPv6HostRoute(
          ifname_downlink,
          *net_base::IPv6CIDR::CreateFromAddressAndPrefix(ip, 128),
          uplink_ip)) {
    // AddIPv6HostRoute with |uplink_ip| could fail because of uplink address
    // missing on the uplink interface (racing between patchpanel and kernel).
    // When that happens fall back to a route without |uplink_ip| to secure
    // critical connectivity for guest. (The `src` specifier part only affects
    // direct communication between guest and the host.)
    LOG(WARNING) << __func__ << ": " << pair
                 << ", Failed to setup the IPv6 route: " << ip << " dev "
                 << ifname_downlink << " src " << uplink_ip_str;
    if (uplink_ip &&
        !datapath_->AddIPv6HostRoute(
            ifname_downlink,
            *net_base::IPv6CIDR::CreateFromAddressAndPrefix(ip, 128))) {
      LOG(WARNING) << __func__ << ": " << pair
                   << ", Failed to setup the IPv6 route: " << ip << " dev "
                   << ifname_downlink;
    }
  }
}

std::optional<std::string> GuestIPv6Service::DownlinkToUplink(
    const std::string& downlink) {
  for (const auto& [upstream_ifname, forward_record] : forward_record_) {
    if (forward_record.downstream_ifnames.find(downlink) !=
        forward_record.downstream_ifnames.end()) {
      return upstream_ifname;
    }
  }
  return std::nullopt;
}

const std::set<std::string>& GuestIPv6Service::UplinkToDownlinks(
    const std::string& uplink) {
  static std::set<std::string> empty_set;

  const auto it = forward_record_.find(uplink);
  if (it != forward_record_.end()) {
    return it->second.downstream_ifnames;
  }
  return empty_set;
}

bool GuestIPv6Service::StartRAServer(const std::string& ifname,
                                     const net_base::IPv6CIDR& prefix,
                                     const std::vector<std::string>& rdnss,
                                     const std::optional<int>& mtu,
                                     const std::optional<int>& hop_limit) {
  base::FilePath run_path(kRadvdRunDir);
  if (!base::DirectoryExists(run_path)) {
    LOG(ERROR) << __func__ << "(" << ifname << "): Configuration directory "
               << kRadvdRunDir << " is not available.";
    return false;
  }
  return CreateConfigFile(ifname, prefix, rdnss, mtu, hop_limit) &&
         StartRadvd(ifname);
}

bool GuestIPv6Service::StopRAServer(const std::string& ifname) {
  const base::FilePath& pid_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdPidFilePrefix) + ifname);

  std::string pid_str;
  pid_t pid;
  if (!base::ReadFileToString(pid_file_path, &pid_str) ||
      !base::TrimString(pid_str, "\n", &pid_str) ||
      !base::StringToInt(pid_str, &pid)) {
    LOG(WARNING) << __func__ << "(" << ifname << "): Invalid radvd pid file "
                 << pid_file_path;
    return false;
  }

  if (!brillo::Process::ProcessExists(pid)) {
    LOG(WARNING) << __func__ << "(" << ifname << "): radvd[" << pid
                 << "] already stopped for interface " << ifname;
    return true;
  }
  brillo::ProcessImpl process;
  process.Reset(pid);
  if (process.Kill(SIGTERM, kTimeoutForSIGTERM.InSeconds())) {
    brillo::DeleteFile(pid_file_path);
    return true;
  }
  LOG(WARNING) << __func__ << "(" << ifname
               << "): Not able to gracefully stop radvd[" << pid
               << "] for interface " << ifname << ", trying to force stop";
  if (process.Kill(SIGKILL, kTimeoutForSIGKILL.InSeconds())) {
    brillo::DeleteFile(pid_file_path);
    return true;
  }
  LOG(ERROR) << __func__ << "(" << ifname << "): Cannot stop radvd[" << pid
             << "] for interface " << ifname;
  return false;
}

namespace {
constexpr char kRadvdConfTemplate[] = R"(interface $1 {
  AdvSendAdvert on;
  prefix $2 {
    AdvOnLink off;
    AdvAutonomous on;
  };
  $3
  $4
  $5
};
)";
}  // namespace

bool GuestIPv6Service::CreateConfigFile(const std::string& ifname,
                                        const net_base::IPv6CIDR& prefix,
                                        const std::vector<std::string>& rdnss,
                                        const std::optional<int>& mtu,
                                        const std::optional<int>& hop_limit) {
  const std::string contents = base::ReplaceStringPlaceholders(
      kRadvdConfTemplate,
      {
          /*$1=*/ifname,
          /*$2=*/prefix.ToString(),
          /*$3=*/(mtu ? base::StringPrintf("AdvLinkMTU %d;", *mtu) : ""),
          /*$4=*/
          (base::StringPrintf(
              "AdvCurHopLimit %d;",
              hop_limit ? *hop_limit : kDefaultDownstreamHopLimit)),
          /*$5=*/
          (!rdnss.empty()
               ? base::StrCat({"RDNSS ", base::JoinString(rdnss, " "), " {};"})
               : ""),
      },
      nullptr);

  const base::FilePath& conf_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdConfigFilePrefix) + ifname);

  return system_->WriteConfigFile(conf_file_path, contents);
}

bool GuestIPv6Service::StartRadvd(const std::string& ifname) {
  const base::FilePath& conf_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdConfigFilePrefix) + ifname);
  const base::FilePath& pid_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdPidFilePrefix) + ifname);

  std::vector<std::string> argv = {
      kRadvdPath, "-n",
      "-C",       conf_file_path.value(),
      "-p",       pid_file_path.value(),
      "-m",       "syslog",
  };

  auto mj = brillo::Minijail::GetInstance();
  minijail* jail = mj->New();
  mj->DropRoot(jail, kPatchpaneldUid, kPatchpaneldGid);
  constexpr uint64_t kNetRawCapMask = CAP_TO_MASK(CAP_NET_RAW);
  mj->UseCapabilities(jail, kNetRawCapMask);

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  pid_t pid;
  bool ran = mj->RunAndDestroy(jail, args, &pid);

  return ran;
}

const std::optional<net_base::IPv6Address> GuestIPv6Service::GetUplinkIp(
    const std::string& ifname) const {
  const auto it = uplink_ips_.find(ifname);
  if (it == uplink_ips_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace patchpanel
