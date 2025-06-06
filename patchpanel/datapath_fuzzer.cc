// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>

#include <memory>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/technology.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "patchpanel/datapath.h"
#include "patchpanel/fake_process_runner.h"
#include "patchpanel/firewall.h"
#include "patchpanel/noop_system.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/subnet.h"

namespace patchpanel {
namespace {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
  base::AtExitManager at_exit;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider provider(data, size);

  int32_t pid = provider.ConsumeIntegral<int32_t>();
  std::string netns_name = provider.ConsumeRandomLengthString(10);
  std::string ifname = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  std::string ifname2 = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  std::string ifname3 = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  std::string bridge = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  uint32_t addr = provider.ConsumeIntegral<uint32_t>();
  int prefix_len = provider.ConsumeIntegralInRange<int>(0, 31);
  const auto ipv4_addr = net_base::IPv4Address(addr);
  const auto cidr =
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(ipv4_addr, prefix_len);

  std::vector<uint8_t> mac_data =
      provider.ConsumeBytes<uint8_t>(net_base::MacAddress::kAddressLength);
  mac_data.resize(net_base::MacAddress::kAddressLength);
  const net_base::MacAddress mac =
      *net_base::MacAddress::CreateFromBytes(mac_data);

  const std::vector<uint8_t> ipv6_addr_bytes =
      provider.ConsumeBytes<uint8_t>(net_base::IPv6Address::kAddressLength);
  const int ipv6_prefix_len = provider.ConsumeIntegralInRange<int>(0, 128);
  const auto ipv6_addr = net_base::IPv6Address::CreateFromBytes(ipv6_addr_bytes)
                             .value_or(net_base::IPv6Address());
  const auto ipv6_cidr = *net_base::IPv6CIDR::CreateFromAddressAndPrefix(
      ipv6_addr, ipv6_prefix_len);
  const std::string ipv6_addr_str = ipv6_addr.ToString();
  bool route_on_vpn = provider.ConsumeBool();

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = pid;
  nsinfo.netns_name = netns_name;
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = ifname;
  nsinfo.route_on_vpn = route_on_vpn;
  nsinfo.host_ifname = ifname2;
  nsinfo.peer_ifname = ifname3;
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(cidr, base::DoNothing());
  nsinfo.peer_mac_addr = mac;

  ShillClient::Device shill_device;
  shill_device.ifname = ifname;
  shill_device.technology = net_base::Technology::kWiFi;
  shill_device.service_path = provider.ConsumeRandomLengthString(10);
  shill_device.ifindex = provider.ConsumeIntegral<int32_t>();

  FakeProcessRunner runner;
  auto firewall = new Firewall();
  NoopSystem system;
  Datapath datapath(&runner, firewall, &system);
  datapath.NetnsAttachName(netns_name, pid);
  datapath.NetnsDeleteName(netns_name);
  datapath.AddBridge(ifname, cidr);
  datapath.RemoveBridge(ifname);
  datapath.AddToBridge(ifname, ifname2);
  datapath.StartRoutingDevice(shill_device, ifname2, TrafficSource::kUnknown);
  datapath.StartRoutingDeviceAsSystem(ifname2, TrafficSource::kUnknown);
  datapath.StartRoutingDeviceAsUser(ifname2, TrafficSource::kUnknown,
                                    ipv4_addr);
  datapath.StopRoutingDevice(ifname2, TrafficSource::kUnknown);
  datapath.StartRoutingNamespace(nsinfo);
  datapath.StopRoutingNamespace(nsinfo);
  datapath.ConnectVethPair(pid, netns_name, ifname, ifname2, mac, cidr,
                           ipv6_cidr, provider.ConsumeBool(),
                           provider.ConsumeBool());
  datapath.RemoveInterface(ifname);
  datapath.AddTunTap(ifname, mac, cidr, "", DeviceMode::kTun);
  datapath.RemoveTunTap(ifname, DeviceMode::kTun);
  datapath.AddTunTap(ifname, mac, cidr, "", DeviceMode::kTap);
  datapath.RemoveTunTap(ifname, DeviceMode::kTap);
  datapath.AddIPv4Route(
      net_base::IPv4Address(provider.ConsumeIntegral<uint32_t>()), cidr);
  datapath.DeleteIPv4Route(
      net_base::IPv4Address(provider.ConsumeIntegral<uint32_t>()), cidr);
  datapath.StartConnectionPinning(shill_device);
  datapath.StopConnectionPinning(shill_device);
  datapath.StartVpnRouting(shill_device);
  datapath.StopVpnRouting(shill_device);
  datapath.MaskInterfaceFlags(ifname, provider.ConsumeIntegral<uint16_t>(),
                              provider.ConsumeIntegral<uint16_t>());
  datapath.AddIPv6HostRoute(ifname, ipv6_cidr);
  datapath.RemoveIPv6HostRoute(ipv6_cidr);
  datapath.AddIPv6Address(ifname, ipv6_addr_str);
  datapath.RemoveIPv6Address(ifname, ipv6_addr_str);
  datapath.StartSourceIPv6PrefixEnforcement(shill_device);
  datapath.StopSourceIPv6PrefixEnforcement(shill_device);
  datapath.UpdateSourceEnforcementIPv6Prefix(shill_device, {ipv6_cidr});
  datapath.AddInboundIPv4DNAT(AutoDNATTarget::kArc, shill_device, ipv4_addr);
  datapath.RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, shill_device, ipv4_addr);

  return 0;
}

}  // namespace
}  // namespace patchpanel
