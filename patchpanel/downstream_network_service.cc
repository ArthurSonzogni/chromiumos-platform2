// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

#include <optional>

#include <base/logging.h>
#include <base/rand_util.h>

namespace patchpanel {

CreateDownstreamNetworkResult DownstreamNetworkResultToUMAEvent(
    patchpanel::DownstreamNetworkResult result) {
  switch (result) {
    case patchpanel::DownstreamNetworkResult::SUCCESS:
      return CreateDownstreamNetworkResult::kSuccess;
    case patchpanel::DownstreamNetworkResult::INVALID_ARGUMENT:
      return CreateDownstreamNetworkResult::kInvalidArgument;
    case patchpanel::DownstreamNetworkResult::INTERFACE_USED:
      return CreateDownstreamNetworkResult::kDownstreamUsed;
    case patchpanel::DownstreamNetworkResult::ERROR:
      return CreateDownstreamNetworkResult::kInternalError;
    case patchpanel::DownstreamNetworkResult::DHCP_SERVER_FAILURE:
      return CreateDownstreamNetworkResult::kDHCPServerFailure;
    case patchpanel::DownstreamNetworkResult::UPSTREAM_UNKNOWN:
      return CreateDownstreamNetworkResult::kUpstreamUnknown;
    case patchpanel::DownstreamNetworkResult::DATAPATH_ERROR:
      return CreateDownstreamNetworkResult::kDatapathError;
    case patchpanel::DownstreamNetworkResult::INVALID_REQUEST:
      return CreateDownstreamNetworkResult::kInvalidRequest;
    default:
      return CreateDownstreamNetworkResult::kUnknown;
  }
}

std::optional<DownstreamNetworkInfo> DownstreamNetworkInfo::Create(
    const TetheredNetworkRequest& request,
    const ShillClient::Device& shill_device) {
  auto info = std::make_optional<DownstreamNetworkInfo>();

  info->topology = DownstreamNetworkTopology::kTethering;
  info->enable_ipv6 = request.enable_ipv6();
  info->upstream_device = shill_device;
  info->downstream_ifname = request.ifname();
  if (request.has_mtu()) {
    info->mtu = request.mtu();
  }

  // Fill the DHCP parameters if needed.
  if (request.has_ipv4_config()) {
    const auto ipv4_config = request.ipv4_config();

    info->enable_ipv4_dhcp = true;
    if (ipv4_config.has_ipv4_subnet()) {
      // Fill the parameters from protobuf.
      const auto ipv4_cidr = net_base::IPv4CIDR::CreateFromBytesAndPrefix(
          ipv4_config.gateway_addr(),
          static_cast<int>(ipv4_config.ipv4_subnet().prefix_len()));
      const auto ipv4_dhcp_start_addr =
          net_base::IPv4Address::CreateFromBytes(ipv4_config.dhcp_start_addr());
      const auto ipv4_dhcp_end_addr =
          net_base::IPv4Address::CreateFromBytes(ipv4_config.dhcp_end_addr());
      if (!ipv4_cidr || !ipv4_dhcp_start_addr || !ipv4_dhcp_end_addr) {
        LOG(ERROR) << "Invalid arguments, gateway_addr: "
                   << ipv4_config.gateway_addr()
                   << ", dhcp_start_addr: " << ipv4_config.dhcp_start_addr()
                   << ", dhcp_end_addr: " << ipv4_config.dhcp_end_addr();
        return std::nullopt;
      }

      info->ipv4_cidr = *ipv4_cidr;
      info->ipv4_dhcp_start_addr = *ipv4_dhcp_start_addr;
      info->ipv4_dhcp_end_addr = *ipv4_dhcp_end_addr;
    } else {
      // Randomly pick a /24 subnet from 172.16.0.0/16 prefix, which is a subnet
      // of the Class B private prefix 172.16.0.0/12.
      const uint8_t x = static_cast<uint8_t>(base::RandInt(0, 255));
      info->ipv4_cidr = *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          net_base::IPv4Address(172, 16, x, 1), 24);
      info->ipv4_dhcp_start_addr = net_base::IPv4Address(172, 16, x, 50);
      info->ipv4_dhcp_end_addr = net_base::IPv4Address(172, 16, x, 150);
    }

    // Fill the DNS server.
    for (const auto& ip_str : ipv4_config.dns_servers()) {
      const auto ip = net_base::IPv4Address::CreateFromBytes(ip_str);
      if (!ip) {
        LOG(WARNING) << "Invalid DNS server, length of IP: " << ip_str.length();
      } else {
        info->dhcp_dns_servers.push_back(*ip);
      }
    }

    // Fill the domain search list.
    info->dhcp_domain_searches = {ipv4_config.domain_searches().begin(),
                                  ipv4_config.domain_searches().end()};

    // Fill the DHCP options.
    for (const auto& option : ipv4_config.options()) {
      info->dhcp_options.emplace_back(option.code(), option.content());
    }

    // TODO(b/239559602) Copy or generate the IPv6 prefix configuration for
    // LocalOnlyHotspot mode.
  }

  return info;
}

std::optional<DownstreamNetworkInfo> DownstreamNetworkInfo::Create(
    const LocalOnlyNetworkRequest& request) {
  auto info = std::make_optional<DownstreamNetworkInfo>();

  info->topology = DownstreamNetworkTopology::kLocalOnly;
  // TODO(b/239559602) Enable IPv6 LocalOnlyNetwork with RAServer
  info->enable_ipv6 = false;
  info->upstream_device = std::nullopt;
  info->downstream_ifname = request.ifname();
  // TODO(b/239559602) Copy IPv4 configuration if any.
  // TODO(b/239559602) Copy IPv6 configuration if any.
  return info;
}

std::optional<DHCPServerController::Config>
DownstreamNetworkInfo::ToDHCPServerConfig() const {
  if (!enable_ipv4_dhcp) {
    return std::nullopt;
  }

  return DHCPServerController::Config::Create(
      ipv4_cidr, ipv4_dhcp_start_addr, ipv4_dhcp_end_addr, dhcp_dns_servers,
      dhcp_domain_searches, mtu, dhcp_options);
}

std::ostream& operator<<(std::ostream& stream,
                         const DownstreamNetworkInfo& info) {
  stream << "{ topology: ";
  switch (info.topology) {
    case DownstreamNetworkTopology::kTethering:
      stream << "Tethering, upstream: ";
      if (info.upstream_device) {
        stream << *info.upstream_device;
      } else {
        stream << "nullopt";
      }
      break;
    case DownstreamNetworkTopology::kLocalOnly:
      stream << "LocalOnlyNetwork";
      break;
  }
  stream << ", downstream: " << info.downstream_ifname;
  stream << ", ipv4 subnet: " << info.ipv4_cidr.GetPrefixAddress() << "/"
         << info.ipv4_cidr.prefix_length();
  stream << ", ipv4 addr: " << info.ipv4_cidr.address();
  stream << ", enable_ipv6: " << std::boolalpha << info.enable_ipv6;
  return stream << "}";
}

}  // namespace patchpanel
