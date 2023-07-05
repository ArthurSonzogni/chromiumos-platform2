// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/proto_utils.h"

#include <memory>

#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "patchpanel/arc_service.h"
#include "patchpanel/crostini_service.h"

namespace patchpanel {

void FillTerminaAllocationProto(const Device& termina_device,
                                TerminaVmStartupResponse* output) {
  DCHECK(termina_device.config().ipv4_subnet());
  DCHECK(termina_device.config().lxd_ipv4_subnet());
  output->set_tap_device_ifname(termina_device.host_ifname());
  FillSubnetProto(termina_device.config().ipv4_subnet()->base_cidr(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(
      termina_device.config().guest_ipv4_addr().ToByteString());
  output->set_gateway_ipv4_address(
      termina_device.config().host_ipv4_addr().ToByteString());
  FillSubnetProto(termina_device.config().lxd_ipv4_subnet()->base_cidr(),
                  output->mutable_container_ipv4_subnet());
  output->set_container_ipv4_address(
      termina_device.config()
          .lxd_ipv4_subnet()
          ->CIDRAtOffset(CrostiniService::kTerminaContainerAddressOffset)
          ->address()
          .ToByteString());
}

void FillParallelsAllocationProto(const Device& parallels_device,
                                  ParallelsVmStartupResponse* output) {
  DCHECK(parallels_device.config().ipv4_subnet());
  output->set_tap_device_ifname(parallels_device.host_ifname());
  FillSubnetProto(parallels_device.config().ipv4_subnet()->base_cidr(),
                  output->mutable_ipv4_subnet());
  output->set_ipv4_address(
      parallels_device.config().guest_ipv4_addr().ToByteString());
}

void FillDeviceProto(const Device& virtual_device,
                     patchpanel::NetworkDevice* output) {
  output->set_ifname(virtual_device.host_ifname());
  // b/273741099: The kInterfaceProperty value must be tracked separately to
  // ensure that patchpanel can advertise it in its virtual NetworkDevice
  // messages in the |phys_ifname| field. This allows ARC and dns-proxy to join
  // shill Device information with patchpanel virtual NetworkDevice information
  // without knowing explicitly about Cellular multiplexed interfaces.
  if (virtual_device.shill_device()) {
    output->set_phys_ifname(
        virtual_device.shill_device()->shill_device_interface_property);
  }
  output->set_guest_ifname(virtual_device.guest_ifname());
  output->set_ipv4_addr(
      virtual_device.config().guest_ipv4_addr().ToInAddr().s_addr);
  output->set_host_ipv4_addr(
      virtual_device.config().host_ipv4_addr().ToInAddr().s_addr);
  switch (virtual_device.type()) {
    case Device::Type::kARC0:
      // The "arc0" legacy management device is not exposed in DBus
      // patchpanel "GetDevices" method or in "NetworkDeviceChanged" signal.
      // However the "arc0" legacy device is exposed in the ArcVmStartupResponse
      // proto sent back to a "ArcVmStartup" method call. In this latter case
      // the |guest_type| field is left empty.
      output->set_phys_ifname(kArc0Ifname);
      break;
    case Device::Type::kARCContainer:
      output->set_guest_type(NetworkDevice::ARC);
      break;
    case Device::Type::kARCVM:
      output->set_guest_type(NetworkDevice::ARCVM);
      break;
    case Device::Type::kTerminaVM:
      output->set_phys_ifname(virtual_device.host_ifname());
      output->set_guest_type(NetworkDevice::TERMINA_VM);
      break;
    case Device::Type::kParallelsVM:
      output->set_phys_ifname(virtual_device.host_ifname());
      output->set_guest_type(NetworkDevice::PARALLELS_VM);
      break;
  }
  if (const auto* subnet = virtual_device.config().ipv4_subnet()) {
    FillSubnetProto(*subnet, output->mutable_ipv4_subnet());
  }
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

void FillDeviceDnsProxyProto(
    const Device& virtual_device,
    patchpanel::NetworkDevice* output,
    const std::map<std::string, net_base::IPv4Address>& ipv4_addrs,
    const std::map<std::string, net_base::IPv6Address>& ipv6_addrs) {
  const auto& ipv4_it = ipv4_addrs.find(virtual_device.host_ifname());
  if (ipv4_it != ipv4_addrs.end()) {
    output->set_dns_proxy_ipv4_addr(ipv4_it->second.ToByteString());
  }
  const auto& ipv6_it = ipv6_addrs.find(virtual_device.host_ifname());
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

}  // namespace patchpanel
