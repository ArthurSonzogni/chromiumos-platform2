// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/guest_ipv6_service.h"

#include <net/ethernet.h>
#include <netinet/in.h>

#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>

#include "patchpanel/ipc.h"
#include "patchpanel/net_util.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

GuestIPv6Service::ForwardMethod GetForwardMethodByDeviceType(
    ShillClient::Device::Type type) {
  switch (type) {
    case ShillClient::Device::Type::kEthernet:
    case ShillClient::Device::Type::kEthernetEap:
    case ShillClient::Device::Type::kWifi:
      return GuestIPv6Service::ForwardMethod::kMethodNDProxy;
    case ShillClient::Device::Type::kCellular:
      // b/187462665, b/187918638: If the physical interface is a cellular
      // modem, the network connection is expected to work as a point to point
      // link where neighbor discovery of the remote gateway is not possible.
      // Therefore force guests are told to see the host as their next hop.
      // TODO(taoyl): Change to kMethodRAServer
      return GuestIPv6Service::ForwardMethod::kMethodNDProxyForCellular;
    default:
      return GuestIPv6Service::ForwardMethod::kMethodUnknown;
  }
}

bool GetLocalMac(const std::string& ifname, MacAddress* mac_addr) {
  auto fd = base::ScopedFD(socket(AF_INET6, SOCK_DGRAM, 0));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "socket() failed for dummy socket";
    return false;
  }
  ifreq ifr = {};
  base::strlcpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ);

  if (ioctl(fd.get(), SIOCGIFHWADDR, &ifr) < 0) {
    PLOG(ERROR) << "ioctl() failed to get MAC address on interface " << ifname;
    return false;
  }
  memcpy(mac_addr->data(), ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
  return true;
}

}  // namespace

GuestIPv6Service::GuestIPv6Service(SubprocessController* nd_proxy,
                                   Datapath* datapath,
                                   ShillClient* shill_client)
    : nd_proxy_(nd_proxy), datapath_(datapath), shill_client_(shill_client) {}

void GuestIPv6Service::Start() {
  nd_proxy_->RegisterFeedbackMessageHandler(base::BindRepeating(
      &GuestIPv6Service::OnNDProxyMessage, weak_factory_.GetWeakPtr()));
  nd_proxy_->Listen();
}

void GuestIPv6Service::StartForwarding(const std::string& ifname_uplink,
                                       const std::string& ifname_downlink,
                                       bool downlink_is_tethering) {
  LOG(INFO) << "Starting IPv6 forwarding between uplink: " << ifname_uplink
            << ", downlink: " << ifname_downlink;
  int if_id_uplink = IfNametoindex(ifname_uplink);
  if (if_id_uplink == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_uplink;
    return;
  }
  if_cache_[ifname_uplink] = if_id_uplink;
  int if_id_downlink = IfNametoindex(ifname_downlink);
  if (if_id_downlink == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_downlink;
    return;
  }
  if_cache_[ifname_downlink] = if_id_downlink;

  // Lookup ForwardEntry for the specified uplink. If it does not exist, create
  // a new one based on its device type.
  ForwardMethod forward_method;
  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it != forward_record_.end()) {
    forward_method = it->method;
    it->downstream_ifnames.insert(ifname_downlink);
  } else {
    ShillClient::Device upstream_shill_device;
    shill_client_->GetDeviceProperties(ifname_uplink, &upstream_shill_device);
    forward_method = GetForwardMethodByDeviceType(upstream_shill_device.type);

    if (forward_method == ForwardMethod::kMethodUnknown) {
      LOG(INFO) << "IPv6 forwarding not supported on device type of "
                << ifname_uplink << ", skipped";
      return;
    }
    forward_record_.push_back(ForwardEntry{
        forward_method, ifname_uplink, std::set<std::string>{ifname_downlink}});
  }

  if (!datapath_->MaskInterfaceFlags(ifname_uplink, IFF_ALLMULTI)) {
    LOG(WARNING) << "Failed to setup all multicast mode for interface "
                 << ifname_uplink;
  }
  if (!datapath_->MaskInterfaceFlags(ifname_downlink, IFF_ALLMULTI)) {
    LOG(WARNING) << "Failed to setup all multicast mode for interface "
                 << ifname_downlink;
  }

  switch (forward_method) {
    case ForwardMethod::kMethodNDProxy:
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA_RS_RA, if_id_uplink,
                         if_id_downlink);
      break;
    case ForwardMethod::kMethodNDProxyForCellular:
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS,
          if_id_uplink, if_id_downlink);
      break;
    case ForwardMethod::kMethodRAServer:
      // No need of proxying between downlink and uplink for RA server.
      break;
    case ForwardMethod::kMethodUnknown:
      NOTREACHED();
  }

  // Start NA proxying between the new downlink and existing downlinks, if any.
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  CHECK(it != forward_record_.end());
  for (const auto& another_downlink : it->downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      int32_t if_id_downlink2 = if_cache_[another_downlink];
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA, if_id_downlink,
                         if_id_downlink2);
    }
  }
}

void GuestIPv6Service::StopForwarding(const std::string& ifname_uplink,
                                      const std::string& ifname_downlink) {
  LOG(INFO) << "Stopping IPv6 forwarding between uplink: " << ifname_uplink
            << ", downlink: " << ifname_downlink;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it == forward_record_.end()) {
    return;
  }
  if (it->downstream_ifnames.find(ifname_downlink) ==
      it->downstream_ifnames.end()) {
    return;
  }

  SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                     if_cache_[ifname_uplink], if_cache_[ifname_downlink]);

  // Remove proxying between specified downlink and all other downlinks in the
  // same group.
  for (const auto& another_downlink : it->downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[ifname_downlink],
                         if_cache_[another_downlink]);
    }
  }

  std::string downlink_addr = downlink_addrs[ifname_downlink];
  if (!downlink_addr.empty()) {
    datapath_->RemoveIPv6Address(ifname_downlink, downlink_addr);
  }
  downlink_addrs.erase(ifname_downlink);

  it->downstream_ifnames.erase(ifname_downlink);
  if (it->downstream_ifnames.empty()) {
    forward_record_.erase(it);
  }
}

void GuestIPv6Service::StopUplink(const std::string& ifname_uplink) {
  LOG(INFO) << "Stopping all IPv6 forwarding with uplink: " << ifname_uplink;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it == forward_record_.end())
    return;

  // Remove proxying between specified uplink and all downlinks.
  for (const auto& ifname_downlink : it->downstream_ifnames) {
    SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                       if_cache_[ifname_uplink], if_cache_[ifname_downlink]);

    std::string downlink_addr = downlink_addrs[ifname_downlink];
    if (!downlink_addr.empty()) {
      datapath_->RemoveIPv6Address(ifname_downlink, downlink_addr);
    }
    downlink_addrs.erase(ifname_downlink);
  }

  // Remove proxying between all downlink pairs in the forward group.
  const auto& downlinks = it->downstream_ifnames;
  for (auto it1 = downlinks.begin(); it1 != downlinks.end(); it1++) {
    for (auto it2 = std::next(it1); it2 != downlinks.end(); it2++) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[it1->c_str()], if_cache_[it2->c_str()]);
    }
  }

  forward_record_.erase(it);
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

void GuestIPv6Service::SetForwardMethod(const std::string& ifname_uplink,
                                        ForwardMethod method) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::SendNDProxyControl(
    NDProxyControlMessage::NDProxyRequestType type,
    int32_t if_id_primary,
    int32_t if_id_secondary) {
  VLOG(4) << "Sending NDProxyControlMessage: " << type << ": " << if_id_primary
          << "<->" << if_id_secondary;
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
    LOG(ERROR) << "Unexpected feedback message type";
    return;
  }

  const NDProxySignalMessage& msg = fm.ndproxy_signal();
  if (msg.has_neighbor_detected_signal()) {
    const auto& inner_msg = msg.neighbor_detected_signal();
    std::string ifname = IfIndextoname(inner_msg.if_id());
    in6_addr ip;
    memcpy(&ip, inner_msg.ip().data(), sizeof(in6_addr));
    std::string ip6_str = IPv6AddressToString(ip);
    if (!datapath_->AddIPv6HostRoute(ifname, ip6_str, 128)) {
      LOG(WARNING) << "Failed to setup the IPv6 route: " << ip6_str << " to "
                   << ifname;
    }
    return;
  }

  if (msg.has_router_detected_signal()) {
    const auto& inner_msg = msg.router_detected_signal();
    std::string ifname = IfIndextoname(inner_msg.if_id());
    in6_addr ip;
    memcpy(&ip, inner_msg.ip().data(), sizeof(in6_addr));
    OnRouterDetected(ifname, ip, inner_msg.prefix_len());
    return;
  }

  LOG(ERROR) << "Unknown NDProxy event ";
  NOTREACHED();
}

void GuestIPv6Service::OnRouterDetected(const std::string& ifname_uplink,
                                        const in6_addr& prefix,
                                        int prefix_len) {
  if (prefix_len > 64) {
    LOG(WARNING) << "Router announced " << IPv6AddressToString(prefix) << "/"
                 << prefix_len << " which is smaller than a /64.";
    return;
  }

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it == forward_record_.end())
    return;

  // Add an EUI64 address onto every downlink. This address will be used when
  // directly communicating with the downstream guest.
  for (const auto& ifname_downlink : it->downstream_ifnames) {
    const std::string& current_downlink_addr = downlink_addrs[ifname_downlink];
    MacAddress local_mac;
    if (!GetLocalMac(ifname_downlink, &local_mac)) {
      continue;
    }
    in6_addr eui64_ip;
    GenerateEUI64Address(&eui64_ip, prefix, local_mac);
    std::string eui64_ip_str = IPv6AddressToString(eui64_ip);
    if (current_downlink_addr == eui64_ip_str) {
      continue;
    }
    if (!current_downlink_addr.empty()) {
      datapath_->RemoveIPv6Address(ifname_downlink, current_downlink_addr);
    }
    if (!datapath_->AddIPv6Address(ifname_downlink, eui64_ip_str)) {
      LOG(WARNING) << "Failed to setup the IPv6 address for interface "
                   << ifname_downlink << ", addr " << eui64_ip_str;
    }
    downlink_addrs[ifname_downlink] = eui64_ip_str;
  }
}

}  // namespace patchpanel
