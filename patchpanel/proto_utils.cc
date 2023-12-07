// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/proto_utils.h"

#include <memory>

#include <net-base/http_url.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>

#include "patchpanel/arc_service.h"
#include "patchpanel/crostini_service.h"

namespace patchpanel {

void FillTerminaAllocationProto(
    const CrostiniService::CrostiniDevice& termina_device,
    TerminaVmStartupResponse* output) {
  DCHECK(termina_device.lxd_ipv4_subnet());
  DCHECK(termina_device.lxd_ipv4_address());
  output->set_tap_device_ifname(termina_device.tap_device_ifname());
  FillSubnetProto(termina_device.vm_ipv4_subnet(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(termina_device.vm_ipv4_address().ToByteString());
  output->set_gateway_ipv4_address(
      termina_device.gateway_ipv4_address().ToByteString());
  FillSubnetProto(*termina_device.lxd_ipv4_subnet(),
                  output->mutable_container_ipv4_subnet());
  output->set_container_ipv4_address(
      termina_device.lxd_ipv4_address()->ToByteString());
}

void FillParallelsAllocationProto(
    const CrostiniService::CrostiniDevice& parallels_device,
    ParallelsVmStartupResponse* output) {
  output->set_tap_device_ifname(parallels_device.tap_device_ifname());
  FillSubnetProto(parallels_device.vm_ipv4_subnet(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(parallels_device.vm_ipv4_address().ToByteString());
}

void FillBruschettaAllocationProto(
    const CrostiniService::CrostiniDevice& bruschetta_device,
    BruschettaVmStartupResponse* output) {
  output->set_tap_device_ifname(bruschetta_device.tap_device_ifname());
  FillSubnetProto(bruschetta_device.vm_ipv4_subnet(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(bruschetta_device.vm_ipv4_address().ToByteString());
  output->set_gateway_ipv4_address(
      bruschetta_device.gateway_ipv4_address().ToByteString());
}

void FillBorealisAllocationProto(
    const CrostiniService::CrostiniDevice& borealis_device,
    BorealisVmStartupResponse* output) {
  output->set_tap_device_ifname(borealis_device.tap_device_ifname());
  FillSubnetProto(borealis_device.vm_ipv4_subnet(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(borealis_device.vm_ipv4_address().ToByteString());
  output->set_gateway_ipv4_address(
      borealis_device.gateway_ipv4_address().ToByteString());
}

void FillSubnetProto(const net_base::IPv4CIDR& cidr,
                     patchpanel::IPv4Subnet* output) {
  output->set_addr(cidr.address().ToByteString());
  output->set_base_addr(cidr.address().ToInAddr().s_addr);
  output->set_prefix_len(static_cast<uint32_t>(cidr.prefix_length()));
}

void FillSubnetProto(const Subnet& virtual_subnet,
                     patchpanel::IPv4Subnet* output) {
  FillSubnetProto(virtual_subnet.base_cidr(), output);
}

void FillArcDeviceDnsProxyProto(
    const ArcService::ArcDevice& arc_device,
    patchpanel::NetworkDevice* output,
    const std::map<std::string, net_base::IPv4Address>& ipv4_addrs,
    const std::map<std::string, net_base::IPv6Address>& ipv6_addrs) {
  const auto& ipv4_it = ipv4_addrs.find(arc_device.bridge_ifname());
  if (ipv4_it != ipv4_addrs.end()) {
    output->set_dns_proxy_ipv4_addr(ipv4_it->second.ToByteString());
  }
  const auto& ipv6_it = ipv6_addrs.find(arc_device.bridge_ifname());
  if (ipv6_it != ipv6_addrs.end()) {
    output->set_dns_proxy_ipv6_addr(ipv6_it->second.ToByteString());
  }
}

void FillDownstreamNetworkProto(
    const DownstreamNetworkInfo& downstream_network_info,
    patchpanel::DownstreamNetwork* output) {
  output->set_downstream_ifname(downstream_network_info.downstream_ifname);
  output->set_ipv4_gateway_addr(
      downstream_network_info.ipv4_cidr.address().ToByteString());
  FillSubnetProto(downstream_network_info.ipv4_cidr,
                  output->mutable_ipv4_subnet());
}

void FillNetworkClientInfoProto(const DownstreamClientInfo& network_client_info,
                                NetworkClientInfo* output) {
  output->set_mac_addr(network_client_info.mac_addr.data(),
                       network_client_info.mac_addr.size());
  output->set_ipv4_addr(network_client_info.ipv4_addr.ToByteString());
  for (const auto& ipv6_addr : network_client_info.ipv6_addresses) {
    output->add_ipv6_addresses(ipv6_addr.ToByteString());
  }
  output->set_hostname(network_client_info.hostname);
  output->set_vendor_class(network_client_info.vendor_class);
}

net_base::NetworkConfig DeserializeNetworkConfig(
    const patchpanel::NetworkConfig& in) {
  net_base::NetworkConfig out;
  if (in.has_ipv4_address()) {
    out.ipv4_address = net_base::IPv4CIDR::CreateFromBytesAndPrefix(
        in.ipv4_address().addr(), in.ipv4_address().prefix_len());
  }
  if (in.has_ipv4_broadcast()) {
    out.ipv4_broadcast =
        net_base::IPv4Address::CreateFromBytes(in.ipv4_broadcast());
  }
  if (in.has_ipv4_gateway()) {
    out.ipv4_gateway =
        net_base::IPv4Address::CreateFromBytes(in.ipv4_gateway());
  }

  for (const auto& ipv6_address : in.ipv6_addresses()) {
    auto ipv6_addr_out = net_base::IPv6CIDR::CreateFromBytesAndPrefix(
        ipv6_address.addr(), ipv6_address.prefix_len());
    if (ipv6_addr_out) {
      out.ipv6_addresses.push_back(*ipv6_addr_out);
    }
  }
  if (in.has_ipv6_gateway()) {
    out.ipv6_gateway =
        net_base::IPv6Address::CreateFromBytes(in.ipv6_gateway());
  }

  out.ipv4_default_route = in.ipv4_default_route();
  out.ipv6_blackhole_route = in.ipv6_blackhole_route();

  for (const auto& prefix : in.excluded_route_prefixes()) {
    auto prefix_out = net_base::IPCIDR::CreateFromBytesAndPrefix(
        prefix.addr(), prefix.prefix_len());
    if (prefix_out) {
      out.excluded_route_prefixes.push_back(*prefix_out);
    }
  }
  for (const auto& prefix : in.included_route_prefixes()) {
    auto prefix_out = net_base::IPCIDR::CreateFromBytesAndPrefix(
        prefix.addr(), prefix.prefix_len());
    if (prefix_out) {
      out.included_route_prefixes.push_back(*prefix_out);
    }
  }
  for (const auto& route : in.rfc3442_routes()) {
    auto prefix = net_base::IPv4CIDR::CreateFromBytesAndPrefix(
        route.prefix().addr(), route.prefix().prefix_len());
    auto gateway = net_base::IPv4Address::CreateFromBytes(route.gateway());
    if (prefix && gateway) {
      out.rfc3442_routes.emplace_back(*prefix, *gateway);
    }
  }

  for (const auto& dns : in.dns_servers()) {
    auto dns_out = net_base::IPAddress::CreateFromBytes(dns);
    if (dns_out) {
      out.dns_servers.push_back(*dns_out);
    }
  }
  for (const auto& dnssl : in.dns_search_domains()) {
    out.dns_search_domains.push_back(dnssl);
  }
  if (in.has_mtu()) {
    out.mtu = in.mtu();
  }

  if (in.has_captive_portal_uri()) {
    out.captive_portal_uri =
        net_base::HttpUrl::CreateFromString(in.captive_portal_uri());
  }

  return out;
}

}  // namespace patchpanel
