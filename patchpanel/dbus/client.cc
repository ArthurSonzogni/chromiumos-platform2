// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <fcntl.h>

#include <algorithm>
#include <optional>
#include <ostream>
#include <vector>

#include <base/barrier_callback.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_util.h>
#include <base/synchronization/waitable_event.h>
#include <base/task/bind_post_task.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_transport.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/technology.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>
#include <patchpanel/proto_bindings/traffic_annotation.pb.h>

#include "patchpanel/dbus-proxies.h"
#include "socketservice/dbus-proxies.h"

namespace patchpanel {

namespace {

using org::chromium::PatchPanelProxyInterface;
using org::chromium::SocketServiceProxyInterface;

patchpanel::TrafficCounter::Source ConvertTrafficSource(
    Client::TrafficSource source) {
  switch (source) {
    case Client::TrafficSource::kUnknown:
      return patchpanel::TrafficCounter::UNKNOWN;
    case Client::TrafficSource::kChrome:
      return patchpanel::TrafficCounter::CHROME;
    case Client::TrafficSource::kUser:
      return patchpanel::TrafficCounter::USER;
    case Client::TrafficSource::kUpdateEngine:
      return patchpanel::TrafficCounter::UPDATE_ENGINE;
    case Client::TrafficSource::kSystem:
      return patchpanel::TrafficCounter::SYSTEM;
    case Client::TrafficSource::kVpn:
      return patchpanel::TrafficCounter::VPN;
    case Client::TrafficSource::kArc:
      return patchpanel::TrafficCounter::ARC;
    case Client::TrafficSource::kBorealisVM:
      return patchpanel::TrafficCounter::BOREALIS_VM;
    case Client::TrafficSource::kBruschettaVM:
      return patchpanel::TrafficCounter::BRUSCHETTA_VM;
    case Client::TrafficSource::kCrostiniVM:
      return patchpanel::TrafficCounter::CROSTINI_VM;
    case Client::TrafficSource::kParallelsVM:
      return patchpanel::TrafficCounter::PARALLELS_VM;
    case Client::TrafficSource::kTethering:
      return patchpanel::TrafficCounter::TETHERING;
    case Client::TrafficSource::kWiFiDirect:
      return patchpanel::TrafficCounter::WIFI_DIRECT;
    case Client::TrafficSource::kWiFiLOHS:
      return patchpanel::TrafficCounter::WIFI_LOHS;
  }
}

Client::TrafficSource ConvertTrafficSource(
    patchpanel::TrafficCounter::Source source) {
  switch (source) {
    case patchpanel::TrafficCounter::CHROME:
      return Client::TrafficSource::kChrome;
    case patchpanel::TrafficCounter::USER:
      return Client::TrafficSource::kUser;
    case patchpanel::TrafficCounter::UPDATE_ENGINE:
      return Client::TrafficSource::kUpdateEngine;
    case patchpanel::TrafficCounter::SYSTEM:
      return Client::TrafficSource::kSystem;
    case patchpanel::TrafficCounter::VPN:
      return Client::TrafficSource::kVpn;
    case patchpanel::TrafficCounter::ARC:
      return Client::TrafficSource::kArc;
    case patchpanel::TrafficCounter::BOREALIS_VM:
      return Client::TrafficSource::kBorealisVM;
    case patchpanel::TrafficCounter::BRUSCHETTA_VM:
      return Client::TrafficSource::kBruschettaVM;
    case patchpanel::TrafficCounter::CROSTINI_VM:
      return Client::TrafficSource::kCrostiniVM;
    case patchpanel::TrafficCounter::PARALLELS_VM:
      return Client::TrafficSource::kParallelsVM;
    case patchpanel::TrafficCounter::TETHERING:
      return Client::TrafficSource::kTethering;
    case patchpanel::TrafficCounter::WIFI_DIRECT:
      return Client::TrafficSource::kWiFiDirect;
    case patchpanel::TrafficCounter::WIFI_LOHS:
      return Client::TrafficSource::kWiFiLOHS;
    default:
      return Client::TrafficSource::kUnknown;
  }
}

patchpanel::NeighborReachabilityEventSignal::Role ConvertNeighborRole(
    Client::NeighborRole role) {
  switch (role) {
    case Client::NeighborRole::kGateway:
      return patchpanel::NeighborReachabilityEventSignal::GATEWAY;
    case Client::NeighborRole::kDnsServer:
      return patchpanel::NeighborReachabilityEventSignal::DNS_SERVER;
    case Client::NeighborRole::kGatewayAndDnsServer:
      return patchpanel::NeighborReachabilityEventSignal::
          GATEWAY_AND_DNS_SERVER;
  }
}

patchpanel::NeighborReachabilityEventSignal::EventType ConvertNeighborStatus(
    Client::NeighborStatus status) {
  switch (status) {
    case Client::NeighborStatus::kFailed:
      return patchpanel::NeighborReachabilityEventSignal::FAILED;
    case Client::NeighborStatus::kReachable:
      return patchpanel::NeighborReachabilityEventSignal::REACHABLE;
  }
}

patchpanel::ModifyPortRuleRequest::Operation ConvertFirewallRequestOperation(
    Client::FirewallRequestOperation op) {
  switch (op) {
    case Client::FirewallRequestOperation::kCreate:
      return ModifyPortRuleRequest::CREATE;
    case Client::FirewallRequestOperation::kDelete:
      return ModifyPortRuleRequest::DELETE;
  }
}

patchpanel::ModifyPortRuleRequest::RuleType ConvertFirewallRequestType(
    Client::FirewallRequestType type) {
  switch (type) {
    case Client::FirewallRequestType::kAccess:
      return ModifyPortRuleRequest::ACCESS;
    case Client::FirewallRequestType::kLockdown:
      return ModifyPortRuleRequest::LOCKDOWN;
    case Client::FirewallRequestType::kForwarding:
      return ModifyPortRuleRequest::FORWARDING;
  }
}

patchpanel::ModifyPortRuleRequest::Protocol ConvertFirewallRequestProtocol(
    Client::FirewallRequestProtocol protocol) {
  switch (protocol) {
    case Client::FirewallRequestProtocol::kTcp:
      return ModifyPortRuleRequest::TCP;
    case Client::FirewallRequestProtocol::kUdp:
      return ModifyPortRuleRequest::UDP;
  }
}

patchpanel::SetDnsRedirectionRuleRequest::RuleType
ConvertDnsRedirectionRequestType(Client::DnsRedirectionRequestType type) {
  switch (type) {
    case Client::DnsRedirectionRequestType::kDefault:
      return patchpanel::SetDnsRedirectionRuleRequest::DEFAULT;
    case Client::DnsRedirectionRequestType::kArc:
      return patchpanel::SetDnsRedirectionRuleRequest::ARC;
    case Client::DnsRedirectionRequestType::kUser:
      return patchpanel::SetDnsRedirectionRuleRequest::USER;
    case Client::DnsRedirectionRequestType::kExcludeDestination:
      return patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION;
  }
}

patchpanel::SetFeatureFlagRequest::FeatureFlag ConvertFeatureFlag(
    Client::FeatureFlag flag) {
  switch (flag) {
    case Client::FeatureFlag::kWiFiQoS:
      return patchpanel::SetFeatureFlagRequest::FeatureFlag::
          SetFeatureFlagRequest_FeatureFlag_WIFI_QOS;
    case Client::FeatureFlag::kClat:
      return patchpanel::SetFeatureFlagRequest::FeatureFlag::
          SetFeatureFlagRequest_FeatureFlag_CLAT;
  }
}

std::optional<net_base::IPv4CIDR> ConvertIPv4Subnet(const IPv4Subnet& in) {
  return net_base::IPv4CIDR::CreateFromBytesAndPrefix(
      in.addr(), static_cast<int>(in.prefix_len()));
}

std::optional<Client::TrafficCounter> ConvertTrafficCounter(
    const TrafficCounter& in) {
  auto out = std::make_optional<Client::TrafficCounter>();
  out->traffic.rx_bytes = in.rx_bytes();
  out->traffic.tx_bytes = in.tx_bytes();
  out->traffic.rx_packets = in.rx_packets();
  out->traffic.tx_packets = in.tx_packets();
  out->ifname = in.device();
  out->source = ConvertTrafficSource(in.source());
  switch (in.ip_family()) {
    case patchpanel::TrafficCounter::IPV4:
      out->ip_family = Client::IPFamily::kIPv4;
      break;
    case patchpanel::TrafficCounter::IPV6:
      out->ip_family = Client::IPFamily::kIPv6;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown IpFamily "
                 << patchpanel::TrafficCounter::IpFamily_Name(in.ip_family());
      return std::nullopt;
  }
  return out;
}

std::optional<Client::VirtualDevice> ConvertVirtualDevice(
    const NetworkDevice& in) {
  auto out = std::make_optional<Client::VirtualDevice>();
  out->ifname = in.ifname();
  out->phys_ifname = in.phys_ifname();
  out->guest_ifname = in.guest_ifname();
  out->ipv4_addr = net_base::IPv4Address(in.ipv4_addr());
  out->host_ipv4_addr = net_base::IPv4Address(in.host_ipv4_addr());
  out->ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());

  out->dns_proxy_ipv4_addr =
      net_base::IPv4Address::CreateFromBytes(in.dns_proxy_ipv4_addr());
  out->dns_proxy_ipv6_addr =
      net_base::IPv6Address::CreateFromBytes(in.dns_proxy_ipv6_addr());
  switch (in.technology_type()) {
    case patchpanel::NetworkDevice::CELLULAR:
      out->technology = net_base::Technology::kCellular;
      break;
    case patchpanel::NetworkDevice::ETHERNET:
      out->technology = net_base::Technology::kEthernet;
      break;
    case patchpanel::NetworkDevice::WIFI:
      out->technology = net_base::Technology::kWiFi;
      break;
    default:
      out->technology = std::nullopt;
      break;
  }

  switch (in.guest_type()) {
    case patchpanel::NetworkDevice::ARC:
      out->guest_type = Client::GuestType::kArcContainer;
      break;
    case patchpanel::NetworkDevice::ARCVM:
      out->guest_type = Client::GuestType::kArcVm;
      break;
    case patchpanel::NetworkDevice::TERMINA_VM:
      out->guest_type = Client::GuestType::kTerminaVm;
      break;
    case patchpanel::NetworkDevice::PARALLELS_VM:
      out->guest_type = Client::GuestType::kParallelsVm;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown GuestType "
                 << patchpanel::NetworkDevice::GuestType_Name(in.guest_type());
      return std::nullopt;
  }
  return out;
}

std::optional<Client::TerminaAllocation> ConvertTerminaAllocation(
    const TerminaVmStartupResponse& in) {
  if (!in.has_ipv4_subnet()) {
    LOG(ERROR) << __func__ << ": No Termina IPv4 subnet found";
    return std::nullopt;
  }
  if (!in.has_container_ipv4_subnet()) {
    LOG(ERROR) << __func__ << ": No Termina container IPv4 subnet found";
    return std::nullopt;
  }
  const auto termina_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  const auto termina_address =
      net_base::IPv4Address::CreateFromBytes(in.ipv4_address());
  const auto gateway_address =
      net_base::IPv4Address::CreateFromBytes(in.gateway_ipv4_address());
  const auto container_subnet = ConvertIPv4Subnet(in.container_ipv4_subnet());
  const auto container_address =
      net_base::IPv4Address::CreateFromBytes(in.container_ipv4_address());
  if (!termina_subnet) {
    LOG(ERROR) << __func__ << ": Invalid Termina IPv4 subnet";
    return std::nullopt;
  }
  if (!termina_address || !termina_subnet->InSameSubnetWith(*termina_address)) {
    LOG(ERROR) << __func__ << ": Invalid Termina IPv4 address";
    return std::nullopt;
  }
  if (!gateway_address || !termina_subnet->InSameSubnetWith(*gateway_address)) {
    LOG(ERROR) << __func__ << ": Invalid Termina gateway IPv4 address";
    return std::nullopt;
  }
  if (!container_subnet) {
    LOG(ERROR) << __func__ << ": Invalid Termina container IPv4 subnet";
    return std::nullopt;
  }
  if (!container_address) {
    LOG(ERROR) << __func__ << ": Invalid Termina container IPv4 address";
    return std::nullopt;
  }
  Client::TerminaAllocation termina_alloc;
  termina_alloc.tap_device_ifname = in.tap_device_ifname();
  termina_alloc.termina_ipv4_subnet = *termina_subnet;
  termina_alloc.termina_ipv4_address = *termina_address;
  termina_alloc.gateway_ipv4_address = *gateway_address;
  termina_alloc.container_ipv4_subnet = *container_subnet;
  termina_alloc.container_ipv4_address = *container_address;
  return termina_alloc;
}

std::optional<Client::ParallelsAllocation> ConvertParallelsAllocation(
    const ParallelsVmStartupResponse& in) {
  if (!in.has_ipv4_subnet()) {
    LOG(ERROR) << __func__ << ": No Parallels IPv4 subnet found";
    return std::nullopt;
  }
  const auto parallels_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  const auto parallels_address =
      net_base::IPv4Address::CreateFromBytes(in.ipv4_address());
  if (!parallels_subnet) {
    LOG(ERROR) << __func__ << ": Invalid Parallels IPv4 subnet";
    return std::nullopt;
  }
  if (!parallels_address ||
      !parallels_subnet->InSameSubnetWith(*parallels_address)) {
    LOG(ERROR) << __func__ << ": Invalid Parallels IPv4 address";
    return std::nullopt;
  }
  Client::ParallelsAllocation parallels_alloc;
  parallels_alloc.tap_device_ifname = in.tap_device_ifname();
  parallels_alloc.parallels_ipv4_subnet = *parallels_subnet;
  parallels_alloc.parallels_ipv4_address = *parallels_address;
  return parallels_alloc;
}

std::optional<Client::BruschettaAllocation> ConvertBruschettaAllocation(
    const BruschettaVmStartupResponse& in) {
  if (in.tap_device_ifname().empty()) {
    LOG(ERROR) << __func__ << ": No Bruschetta device interface found";
    return std::nullopt;
  }
  if (!in.has_ipv4_subnet()) {
    LOG(ERROR) << __func__ << ": No Bruschetta IPv4 subnet found";
    return std::nullopt;
  }
  const auto bruschetta_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  const auto bruschetta_address =
      net_base::IPv4Address::CreateFromBytes(in.ipv4_address());
  const auto gateway_address =
      net_base::IPv4Address::CreateFromBytes(in.gateway_ipv4_address());
  if (!bruschetta_subnet) {
    LOG(ERROR) << __func__ << ": Invalid Bruschetta IPv4 subnet";
    return std::nullopt;
  }
  if (!bruschetta_address ||
      !bruschetta_subnet->InSameSubnetWith(*bruschetta_address)) {
    LOG(ERROR) << __func__ << ": Invalid Bruschetta IPv4 address";
    return std::nullopt;
  }
  if (!gateway_address ||
      !bruschetta_subnet->InSameSubnetWith(*gateway_address)) {
    LOG(ERROR) << __func__ << ": Invalid Bruschetta gateway IPv4 address";
    return std::nullopt;
  }

  Client::BruschettaAllocation bruschetta_alloc;
  bruschetta_alloc.tap_device_ifname = in.tap_device_ifname();
  bruschetta_alloc.bruschetta_ipv4_subnet = *bruschetta_subnet;
  bruschetta_alloc.bruschetta_ipv4_address = *bruschetta_address;
  bruschetta_alloc.gateway_ipv4_address = *gateway_address;
  return bruschetta_alloc;
}

std::optional<Client::BorealisAllocation> ConvertBorealisAllocation(
    const BorealisVmStartupResponse& in) {
  if (in.tap_device_ifname().empty()) {
    LOG(ERROR) << __func__ << ": No Borealis device interface found";
    return std::nullopt;
  }
  if (!in.has_ipv4_subnet()) {
    LOG(ERROR) << __func__ << ": No Borealis IPv4 subnet found";
    return std::nullopt;
  }
  const auto borealis_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  const auto borealis_address =
      net_base::IPv4Address::CreateFromBytes(in.ipv4_address());
  const auto gateway_address =
      net_base::IPv4Address::CreateFromBytes(in.gateway_ipv4_address());
  if (!borealis_subnet) {
    LOG(ERROR) << __func__ << ": Invalid Borealis IPv4 subnet";
    return std::nullopt;
  }
  if (!borealis_address ||
      !borealis_subnet->InSameSubnetWith(*borealis_address)) {
    LOG(ERROR) << __func__ << ": Invalid Borealis IPv4 address";
    return std::nullopt;
  }
  if (!gateway_address ||
      !borealis_subnet->InSameSubnetWith(*gateway_address)) {
    LOG(ERROR) << __func__ << ": Invalid Borealis gateway IPv4 address";
    return std::nullopt;
  }

  Client::BorealisAllocation borealis_alloc;
  borealis_alloc.tap_device_ifname = in.tap_device_ifname();
  borealis_alloc.borealis_ipv4_subnet = *borealis_subnet;
  borealis_alloc.borealis_ipv4_address = *borealis_address;
  borealis_alloc.gateway_ipv4_address = *gateway_address;
  return borealis_alloc;
}

std::optional<Client::NetworkClientInfo> ConvertNetworkClientInfo(
    const NetworkClientInfo& in) {
  auto out = std::make_optional<Client::NetworkClientInfo>();
  std::copy(in.mac_addr().begin(), in.mac_addr().end(),
            std::back_inserter(out->mac_addr));
  const auto ipv4_addr = net_base::IPv4Address::CreateFromBytes(in.ipv4_addr());
  if (!ipv4_addr) {
    LOG(ERROR) << "Failed to convert protobuf bytes to IPv4Address. size="
               << in.ipv4_addr().size();
    return std::nullopt;
  }
  out->ipv4_addr = *ipv4_addr;
  for (const auto& in_ipv6_addr : in.ipv6_addresses()) {
    const auto ipv6_addr = net_base::IPv6Address::CreateFromBytes(in_ipv6_addr);
    if (!ipv6_addr) {
      LOG(ERROR) << "Failed to convert protobuf bytes to IPv6Address. size="
                 << in_ipv6_addr.size();
      return std::nullopt;
    }
    out->ipv6_addresses.push_back(*ipv6_addr);
  }
  out->hostname = in.hostname();
  out->vendor_class = in.vendor_class();
  return out;
}

std::optional<Client::DownstreamNetwork> ConvertDownstreamNetwork(
    const DownstreamNetwork& in) {
  auto out = std::make_optional<Client::DownstreamNetwork>();
  out->network_id = in.network_id();
  out->ifname = in.downstream_ifname();

  const auto ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  if (!ipv4_subnet) {
    LOG(ERROR) << "Failed to create IPv4CIDR for ipv4_subnet";
    return std::nullopt;
  }
  out->ipv4_subnet = *ipv4_subnet;

  const auto ipv4_gateway_addr =
      net_base::IPv4Address::CreateFromBytes(in.ipv4_gateway_addr());
  if (!ipv4_gateway_addr) {
    LOG(ERROR) << "Failed to create IPv4Address for gateway address: size="
               << in.ipv4_gateway_addr().size();
    return std::nullopt;
  }
  out->ipv4_gateway_addr = *ipv4_gateway_addr;
  return out;
}

patchpanel::NetworkTechnology ConvertNetworkTechnology(
    Client::NetworkTechnology network_technology) {
  switch (network_technology) {
    case Client::NetworkTechnology::kCellular:
      return patchpanel::NetworkTechnology::CELLULAR;
    case Client::NetworkTechnology::kEthernet:
      return patchpanel::NetworkTechnology::ETHERNET;
    case Client::NetworkTechnology::kVPN:
      return patchpanel::NetworkTechnology::VPN;
    case Client::NetworkTechnology::kWiFi:
      return patchpanel::NetworkTechnology::WIFI;
  }
}

TagSocketRequest::VpnRoutingPolicy ConvertVpnRoutingPolicy(
    Client::VpnRoutingPolicy policy) {
  switch (policy) {
    case Client::VpnRoutingPolicy::kDefaultRouting:
      return TagSocketRequest::DEFAULT_ROUTING;
    case Client::VpnRoutingPolicy::kBypassVpn:
      return TagSocketRequest::BYPASS_VPN;
    case Client::VpnRoutingPolicy::kRouteOnVpn:
      return TagSocketRequest::ROUTE_ON_VPN;
  }
}

traffic_annotation::TrafficAnnotation::Id ConvertTrafficAnnotationId(
    Client::TrafficAnnotationId id) {
  switch (id) {
    case Client::TrafficAnnotationId::kUnspecified:
      return traffic_annotation::TrafficAnnotation::UNSPECIFIED;
    case Client::TrafficAnnotationId::kShillPortalDetector:
      return traffic_annotation::TrafficAnnotation::SHILL_PORTAL_DETECTOR;
    case Client::TrafficAnnotationId::kShillCapportClient:
      return traffic_annotation::TrafficAnnotation::SHILL_CAPPORT_CLIENT;
    case Client::TrafficAnnotationId::kShillCarrierEntitlement:
      return traffic_annotation::TrafficAnnotation::SHILL_CARRIER_ENTITLEMENT;
  }
}

std::optional<Client::NeighborReachabilityEvent>
ConvertNeighborReachabilityEvent(const NeighborReachabilityEventSignal& in) {
  auto out = std::make_optional<Client::NeighborReachabilityEvent>();
  out->ifindex = in.ifindex();
  out->ip_addr = in.ip_addr();
  switch (in.role()) {
    case patchpanel::NeighborReachabilityEventSignal::GATEWAY:
      out->role = Client::NeighborRole::kGateway;
      break;
    case patchpanel::NeighborReachabilityEventSignal::DNS_SERVER:
      out->role = Client::NeighborRole::kDnsServer;
      break;
    case patchpanel::NeighborReachabilityEventSignal::GATEWAY_AND_DNS_SERVER:
      out->role = Client::NeighborRole::kGatewayAndDnsServer;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown NeighborReachability role "
                 << patchpanel::NeighborReachabilityEventSignal::Role_Name(
                        in.role());
      return std::nullopt;
  }
  switch (in.type()) {
    case patchpanel::NeighborReachabilityEventSignal::FAILED:
      out->status = Client::NeighborStatus::kFailed;
      break;
    case patchpanel::NeighborReachabilityEventSignal::REACHABLE:
      out->status = Client::NeighborStatus::kReachable;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown NeighborReachability event type "
                 << patchpanel::NeighborReachabilityEventSignal::EventType_Name(
                        in.type());
      return std::nullopt;
  }
  return out;
}

std::optional<Client::VirtualDeviceEvent> ConvertVirtualDeviceEvent(
    const NetworkDeviceChangedSignal& in) {
  switch (in.event()) {
    case patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED:
      return Client::VirtualDeviceEvent::kAdded;
    case patchpanel::NetworkDeviceChangedSignal::DEVICE_REMOVED:
      return Client::VirtualDeviceEvent::kRemoved;
    default:
      LOG(ERROR) << __func__ << ": Unknown NetworkDeviceChangedSignal event "
                 << patchpanel::NetworkDeviceChangedSignal::Event_Name(
                        in.event());
      return std::nullopt;
  }
}

std::optional<Client::ConnectedNamespace> ConvertConnectedNamespace(
    const ConnectNamespaceResponse& in) {
  auto out = std::make_optional<Client::ConnectedNamespace>();

  const auto ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  if (!ipv4_subnet) {
    LOG(ERROR) << "Failed to create IPv4CIDR for ipv4_subnet";
    return std::nullopt;
  }
  out->ipv4_subnet = *ipv4_subnet;

  out->peer_ifname = in.peer_ifname();
  out->peer_ipv4_address = net_base::IPv4Address(in.peer_ipv4_address());
  out->host_ifname = in.host_ifname();
  out->host_ipv4_address = net_base::IPv4Address(in.host_ipv4_address());
  out->netns_name = in.netns_name();
  return out;
}

std::ostream& operator<<(std::ostream& stream,
                         const ModifyPortRuleRequest& request) {
  stream << "{ operation: "
         << ModifyPortRuleRequest::Operation_Name(request.op())
         << ", rule type: "
         << ModifyPortRuleRequest::RuleType_Name(request.type())
         << ", protocol: "
         << ModifyPortRuleRequest::Protocol_Name(request.proto());
  if (!request.input_ifname().empty()) {
    stream << ", input interface name: " << request.input_ifname();
  }
  if (!request.input_dst_ip().empty()) {
    stream << ", input destination IP: " << request.input_dst_ip();
  }
  stream << ", input destination port: " << request.input_dst_port();
  if (!request.dst_ip().empty()) {
    stream << ", destination IP: " << request.dst_ip();
  }
  if (request.dst_port() != 0) {
    stream << ", destination port: " << request.dst_port();
  }
  stream << " }";
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const SetDnsRedirectionRuleRequest& request) {
  stream << "{ proxy type: "
         << SetDnsRedirectionRuleRequest::RuleType_Name(request.type());
  if (!request.input_ifname().empty()) {
    stream << ", input interface name: " << request.input_ifname();
  }
  if (!request.proxy_address().empty()) {
    stream << ", proxy IPv4 address: " << request.proxy_address();
  }
  if (!request.nameservers().empty()) {
    std::vector<std::string> nameservers;
    for (const auto& ns : request.nameservers()) {
      nameservers.emplace_back(ns);
    }
    stream << ", nameserver(s): " << base::JoinString(nameservers, ",");
  }
  stream << " }";
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const Client::FeatureFlag flag) {
  switch (flag) {
    case Client::FeatureFlag::kWiFiQoS:
      return stream << "WiFiQoS";
    case Client::FeatureFlag::kClat:
      return stream << "Clat";
  }
}

// Prepares a pair of ScopedFDs corresponding to the write end (pair first
// element) and read end (pair second element) of a Linux pipe. The client must
// keep the write end alive until the setup requested from patchpanel is not
// necessary anymore.
std::pair<base::ScopedFD, base::ScopedFD> CreateLifelineFd() {
  int pipe_fds[2] = {-1, -1};
  if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
    PLOG(ERROR) << "Failed to create a pair of fds with pipe2()";
    return {};
  }
  return {base::ScopedFD(pipe_fds[0]), base::ScopedFD(pipe_fds[1])};
}

void OnGetTrafficCountersDBusResponse(
    Client::GetTrafficCountersCallback callback,
    const TrafficCountersResponse& response) {
  std::vector<Client::TrafficCounter> counters;
  for (const auto& proto_counter : response.counters()) {
    auto client_counter = ConvertTrafficCounter(proto_counter);
    if (client_counter) {
      counters.push_back(*client_counter);
    }
  }
  std::move(callback).Run(counters);
}

void OnGetTrafficCountersError(Client::GetTrafficCountersCallback callback,
                               brillo::Error* error) {
  LOG(ERROR) << __func__ << "(): " << error->GetMessage();
  std::move(callback).Run({});
}

void OnNetworkDeviceChanged(
    Client::VirtualDeviceEventHandler handler,
    const patchpanel::NetworkDeviceChangedSignal& signal) {
  const auto event = ConvertVirtualDeviceEvent(signal);
  if (!event) {
    return;
  }

  const auto device = ConvertVirtualDevice(signal.device());
  if (!device) {
    return;
  }

  handler.Run(*event, *device);
}

void OnNeighborReachabilityEvent(
    const Client::NeighborReachabilityEventHandler& handler,
    const NeighborReachabilityEventSignal& signal) {
  const auto event = ConvertNeighborReachabilityEvent(signal);
  if (event) {
    handler.Run(*event);
  }
}

void OnSignalConnectedCallback(const std::string& interface_name,
                               const std::string& signal_name,
                               bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to " << signal_name;
  }
}

// Helper static function to process answers to CreateTetheredNetwork calls.
void OnTetheredNetworkResponse(Client::CreateTetheredNetworkCallback callback,
                               base::ScopedFD fd_local,
                               const TetheredNetworkResponse& response) {
  if (response.response_code() != DownstreamNetworkResult::SUCCESS) {
    LOG(ERROR) << kCreateTetheredNetworkMethod << " failed: "
               << patchpanel::DownstreamNetworkResult_Name(
                      response.response_code());
    std::move(callback).Run({}, {});
    return;
  }

  std::optional<Client::DownstreamNetwork> downstream_network =
      ConvertDownstreamNetwork(response.downstream_network());
  if (!downstream_network) {
    std::move(callback).Run({}, {});
    return;
  }

  std::move(callback).Run(std::move(fd_local), *downstream_network);
}

void OnTetheredNetworkError(Client::CreateTetheredNetworkCallback callback,
                            brillo::Error* error) {
  LOG(ERROR) << __func__ << "(): " << error->GetMessage();
  std::move(callback).Run({}, {});
}

// Helper static function to process answers to CreateLocalOnlyNetwork calls.
void OnLocalOnlyNetworkResponse(Client::CreateLocalOnlyNetworkCallback callback,
                                base::ScopedFD fd_local,
                                const LocalOnlyNetworkResponse& response) {
  if (response.response_code() != DownstreamNetworkResult::SUCCESS) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod << " failed: "
               << patchpanel::DownstreamNetworkResult_Name(
                      response.response_code());
    std::move(callback).Run({}, {});
    return;
  }

  std::optional<Client::DownstreamNetwork> downstream_network =
      ConvertDownstreamNetwork(response.downstream_network());
  if (!downstream_network) {
    std::move(callback).Run({}, {});
    return;
  }

  std::move(callback).Run(std::move(fd_local), *downstream_network);
}

void OnLocalOnlyNetworkError(Client::CreateLocalOnlyNetworkCallback callback,
                             brillo::Error* error) {
  LOG(ERROR) << __func__ << "(): " << error->GetMessage();
  std::move(callback).Run({}, {});
}

// Helper static function to process answers to GetDownstreamNetworkInfo calls.
void OnGetDownstreamNetworkInfoResponse(
    Client::GetDownstreamNetworkInfoCallback callback,
    const GetDownstreamNetworkInfoResponse& response) {
  std::optional<Client::DownstreamNetwork> downstream_network =
      ConvertDownstreamNetwork(response.downstream_network());
  if (!downstream_network) {
    std::move(callback).Run(false, {}, {});
    return;
  }

  std::vector<Client::NetworkClientInfo> clients_info;
  for (const auto& ci : response.clients_info()) {
    const auto info = ConvertNetworkClientInfo(ci);
    if (info) {
      clients_info.push_back(*info);
    }
  }

  std::move(callback).Run(true, *downstream_network, clients_info);
}

void OnGetDownstreamNetworkInfoError(
    Client::GetDownstreamNetworkInfoCallback callback, brillo::Error* error) {
  LOG(ERROR) << __func__ << "(): " << error->GetMessage();
  std::move(callback).Run(false, {}, {});
}

void OnConfigureNetworkResponse(
    Client::ConfigureNetworkCallback callback,
    const std::string& ifname,
    const patchpanel::ConfigureNetworkResponse& response) {
  if (!response.success()) {
    LOG(ERROR) << __func__ << ": Failed to configure Network on " << ifname;
    std::move(callback).Run(false);
    return;
  }
  std::move(callback).Run(true);
}

void OnConfigureNetworkError(Client::ConfigureNetworkCallback callback,
                             const std::string& ifname,
                             brillo::Error* error) {
  LOG(ERROR) << __func__ << "() on " << ifname << ": " << error->GetMessage();
  std::move(callback).Run(false);
}

class ClientImpl : public Client {
 public:
  ClientImpl(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<org::chromium::PatchPanelProxyInterface> pp_proxy,
      std::unique_ptr<org::chromium::SocketServiceProxyInterface> ss_proxy,
      bool owns_bus)
      : bus_(std::move(bus)),
        pp_proxy_(std::move(pp_proxy)),
        ss_proxy_(std::move(ss_proxy)),
        owns_bus_(owns_bus) {}

  ClientImpl(const ClientImpl&) = delete;
  ClientImpl& operator=(const ClientImpl&) = delete;

  ~ClientImpl() override;

  void RegisterOnAvailableCallback(
      base::OnceCallback<void(bool)> callback) override;

  void RegisterProcessChangedCallback(
      base::RepeatingCallback<void(bool)> callback) override;

  bool NotifyArcStartup(pid_t pid) override;
  bool NotifyArcShutdown() override;

  std::optional<Client::ArcVMAllocation> NotifyArcVmStartup(
      uint32_t cid) override;
  bool NotifyArcVmShutdown(uint32_t cid) override;

  std::optional<Client::TerminaAllocation> NotifyTerminaVmStartup(
      uint32_t cid) override;
  bool NotifyTerminaVmShutdown(uint32_t cid) override;

  std::optional<ParallelsAllocation> NotifyParallelsVmStartup(
      uint64_t vm_id, int subnet_index) override;
  bool NotifyParallelsVmShutdown(uint64_t vm_id) override;

  std::optional<BruschettaAllocation> NotifyBruschettaVmStartup(
      uint64_t vm_id) override;
  bool NotifyBruschettaVmShutdown(uint64_t vm_id) override;

  std::optional<BorealisAllocation> NotifyBorealisVmStartup(
      uint32_t vm_id) override;
  bool NotifyBorealisVmShutdown(uint32_t vm_id) override;

  std::pair<base::ScopedFD, Client::ConnectedNamespace> ConnectNamespace(
      pid_t pid,
      const std::string& outbound_ifname,
      bool forward_user_traffic,
      bool route_on_vpn,
      Client::TrafficSource traffic_source,
      bool static_ipv6) override;

  void GetTrafficCounters(const std::set<std::string>& devices,
                          GetTrafficCountersCallback callback) override;

  bool ModifyPortRule(Client::FirewallRequestOperation op,
                      Client::FirewallRequestType type,
                      Client::FirewallRequestProtocol proto,
                      const std::string& input_ifname,
                      const std::string& input_dst_ip,
                      uint32_t input_dst_port,
                      const std::string& dst_ip,
                      uint32_t dst_port) override;

  void SetVpnLockdown(bool enable) override;

  base::ScopedFD RedirectDns(Client::DnsRedirectionRequestType type,
                             const std::string& input_ifname,
                             const std::string& proxy_address,
                             const std::vector<std::string>& nameservers,
                             const std::string& host_ifname) override;

  std::vector<Client::VirtualDevice> GetDevices() override;

  void RegisterVirtualDeviceEventHandler(
      VirtualDeviceEventHandler handler) override;

  void RegisterNeighborReachabilityEventHandler(
      NeighborReachabilityEventHandler handler) override;

  bool CreateTetheredNetwork(
      const std::string& downstream_ifname,
      const std::string& upstream_ifname,
      const std::optional<DHCPOptions>& dhcp_options,
      const std::optional<UplinkIPv6Configuration>& uplink_ipv6_config,
      const std::optional<int>& mtu,
      CreateTetheredNetworkCallback callback) override;

  bool CreateLocalOnlyNetwork(const std::string& ifname,
                              CreateLocalOnlyNetworkCallback callback) override;

  bool GetDownstreamNetworkInfo(
      const std::string& ifname,
      GetDownstreamNetworkInfoCallback callback) override;

  bool ConfigureNetwork(int interface_index,
                        std::string_view interface_name,
                        uint32_t area,
                        const net_base::NetworkConfig& network_config,
                        net_base::NetworkPriority priority,
                        NetworkTechnology technology,
                        int session_id,
                        ConfigureNetworkCallback callback) override;

  bool SendSetFeatureFlagRequest(FeatureFlag flag, bool enable) override;

  bool TagSocket(base::ScopedFD fd,
                 std::optional<int> network_id,
                 std::optional<VpnRoutingPolicy> vpn_policy,
                 std::optional<TrafficAnnotation> traffic_annotation) override;

  void PrepareTagSocket(
      const TrafficAnnotation& annotation,
      std::shared_ptr<brillo::http::Transport> transport) override;

 private:
  // Runs the |task| on the DBus thread synchronously.
  // The generated proxy uses brillo::dbus_utils::CallMethod*(), which asserts
  // to be executed on the DBus thread, instead of hopping on the DBus thread.
  // Therefore we need to do it by ourselves.
  bool RunOnDBusThreadSync(base::OnceCallback<bool()> task) {
    if (!bus_->HasDBusThread() ||
        bus_->GetDBusTaskRunner()->RunsTasksInCurrentSequence()) {
      return std::move(task).Run();
    }

    base::WaitableEvent event;
    bool result = false;
    bus_->GetDBusTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::OnceCallback<bool()> task, bool* result,
                          base::WaitableEvent* event) {
                         *result = std::move(task).Run();
                         event->Signal();
                       },
                       std::move(task), base::Unretained(&result),
                       base::Unretained(&event)));
    event.Wait();
    return result;
  }

  // Runs the |task| on the DBus thread asynchronously.
  // The generated proxy uses brillo::dbus_utils::CallMethod*(), which asserts
  // to be executed on the DBus thread, instead of hopping on the DBus thread.
  // Therefore we need to do it by ourselves.
  void RunOnDBusThreadAsync(base::OnceClosure task) {
    if (!bus_->HasDBusThread() ||
        bus_->GetDBusTaskRunner()->RunsTasksInCurrentSequence()) {
      std::move(task).Run();
      return;
    }

    bus_->GetDBusTaskRunner()->PostTask(FROM_HERE, std::move(task));
  }

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::PatchPanelProxyInterface> pp_proxy_;
  std::unique_ptr<org::chromium::SocketServiceProxyInterface> ss_proxy_;
  bool owns_bus_;  // Yes if |bus_| is created by Client::New

  base::RepeatingCallback<void(bool)> owner_callback_;

  void OnOwnerChanged(const std::string& old_owner,
                      const std::string& new_owner);

  base::WeakPtrFactory<ClientImpl> weak_factory_{this};
};

ClientImpl::~ClientImpl() {
  if (bus_ && owns_bus_) {
    bus_->ShutdownAndBlock();
  }
}

void ClientImpl::RegisterOnAvailableCallback(
    base::OnceCallback<void(bool)> callback) {
  auto done_cb = base::BindOnce(
      [](base::OnceCallback<void(bool)> cb, const std::vector<bool>& results) {
        bool result = false;
        for (const auto r : results) {
          result = result || r;
        }
        std::move(cb).Run(result);
      },
      std::move(callback));
  // |ready_cb| will be called twice, will collect the boolean results and will
  // call |done_cb| with the list of results.
  auto ready_cb = base::BarrierCallback<bool>(2, std::move(done_cb));

  auto* pp_object_proxy = pp_proxy_->GetObjectProxy();
  if (!pp_object_proxy) {
    LOG(ERROR) << "Cannot register callback - no patchpanel proxy";
    return;
  }
  pp_object_proxy->WaitForServiceToBeAvailable(ready_cb);

  auto* ss_object_proxy = ss_proxy_->GetObjectProxy();
  if (!ss_object_proxy) {
    LOG(ERROR) << "Cannot register callback - no socketservice proxy";
    return;
  }
  ss_object_proxy->WaitForServiceToBeAvailable(ready_cb);
}

void ClientImpl::RegisterProcessChangedCallback(
    base::RepeatingCallback<void(bool)> callback) {
  owner_callback_ = callback;
  bus_->GetObjectProxy(kPatchPanelServiceName, dbus::ObjectPath{"/"})
      ->SetNameOwnerChangedCallback(base::BindRepeating(
          &ClientImpl::OnOwnerChanged, weak_factory_.GetWeakPtr()));
}

void ClientImpl::OnOwnerChanged(const std::string& old_owner,
                                const std::string& new_owner) {
  if (new_owner.empty()) {
    LOG(INFO) << "Patchpanel lost";
    if (!owner_callback_.is_null()) {
      owner_callback_.Run(false);
    }
    return;
  }

  LOG(INFO) << "Patchpanel reset";
  if (!owner_callback_.is_null()) {
    owner_callback_.Run(true);
  }
}

bool ClientImpl::NotifyArcStartup(pid_t pid) {
  ArcStartupRequest request;
  request.set_pid(pid);

  // TODO(b/284076578): Check if we can call the DBus method asynchronously.
  ArcStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const ArcStartupRequest& request,
         ArcStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->ArcStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "ARC network startup failed: " << error->GetMessage();
    return false;
  }

  return true;
}

bool ClientImpl::NotifyArcShutdown() {
  // TODO(b/284076578): Check if we can call the DBus method asynchronously.
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, brillo::ErrorPtr* error) {
        ArcShutdownResponse response;
        return proxy->ArcShutdown({}, &response, error);
      },
      pp_proxy_.get(), &error));
  if (!result) {
    LOG(ERROR) << "ARC network shutdown failed: " << error->GetMessage();
    return false;
  }

  return true;
}

std::optional<Client::ArcVMAllocation> ClientImpl::NotifyArcVmStartup(
    uint32_t cid) {
  ArcVmStartupRequest request;
  request.set_cid(cid);

  // TODO(b/284076578): Check if concierge can handle the result asynchronously.
  ArcVmStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const ArcVmStartupRequest& request,
         ArcVmStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->ArcVmStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "ARCVM network startup failed: " << error->GetMessage();
    return std::nullopt;
  }

  const auto arc0_addr =
      net_base::IPv4Address::CreateFromBytes(response.arc0_ipv4_address());
  if (!arc0_addr) {
    LOG(ERROR) << "Could not deserialize arc0 IPv4 address";
    return std::nullopt;
  }

  ArcVMAllocation arcvm_alloc;
  arcvm_alloc.arc0_ipv4_address = *arc0_addr;
  for (const auto& tap : response.tap_device_ifnames()) {
    arcvm_alloc.tap_device_ifnames.push_back(tap);
  }
  return arcvm_alloc;
}

bool ClientImpl::NotifyArcVmShutdown(uint32_t cid) {
  ArcVmShutdownRequest request;
  request.set_cid(cid);

  // TODO(b/284076578): Check if concierge can handle the result asynchronously.
  ArcVmShutdownResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const ArcVmShutdownRequest& request,
         ArcVmShutdownResponse* response, brillo::ErrorPtr* error) {
        return proxy->ArcVmShutdown(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "ARCVM network shutdown failed: " << error->GetMessage();
  }

  return result;
}

std::optional<Client::TerminaAllocation> ClientImpl::NotifyTerminaVmStartup(
    uint32_t cid) {
  TerminaVmStartupRequest request;
  request.set_cid(cid);

  TerminaVmStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const TerminaVmStartupRequest& request,
         TerminaVmStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->TerminaVmStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));

  if (!result) {
    LOG(ERROR) << __func__ << "(cid: " << cid
               << "): TerminaVM network startup failed: "
               << error->GetMessage();
    return std::nullopt;
  }

  const auto termina_alloc = ConvertTerminaAllocation(response);
  if (!termina_alloc) {
    LOG(ERROR) << __func__ << "(cid: " << cid
               << "): Failed to convert network allocation";
  }
  return termina_alloc;
}

bool ClientImpl::NotifyTerminaVmShutdown(uint32_t cid) {
  TerminaVmShutdownRequest request;
  request.set_cid(cid);

  TerminaVmShutdownResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const TerminaVmShutdownRequest& request,
         TerminaVmShutdownResponse* response, brillo::ErrorPtr* error) {
        return proxy->TerminaVmShutdown(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "TerminaVM network shutdown failed: " << error->GetMessage();
    return false;
  }
  return true;
}

std::optional<Client::ParallelsAllocation> ClientImpl::NotifyParallelsVmStartup(
    uint64_t vm_id, int subnet_index) {
  ParallelsVmStartupRequest request;
  request.set_id(vm_id);
  request.set_subnet_index(subnet_index);

  ParallelsVmStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const ParallelsVmStartupRequest& request,
         ParallelsVmStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->ParallelsVmStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));

  if (!result) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << ", subnet_index: " << subnet_index
               << "): Parallels VM network startup failed: "
               << error->GetMessage();
    return std::nullopt;
  }

  const auto network_alloc = ConvertParallelsAllocation(response);
  if (!network_alloc) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << ", subnet_index: " << subnet_index
               << "): Failed to convert Parallels VM network configuration";
  }
  return network_alloc;
}

bool ClientImpl::NotifyParallelsVmShutdown(uint64_t vm_id) {
  ParallelsVmShutdownRequest request;
  request.set_id(vm_id);

  ParallelsVmShutdownResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const ParallelsVmShutdownRequest& request,
         ParallelsVmShutdownResponse* response, brillo::ErrorPtr* error) {
        return proxy->ParallelsVmShutdown(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "ParallelsVM network shutdown failed: "
               << error->GetMessage();
    return false;
  }
  return true;
}

std::optional<Client::BruschettaAllocation>
ClientImpl::NotifyBruschettaVmStartup(uint64_t vm_id) {
  BruschettaVmStartupRequest request;
  request.set_id(vm_id);

  BruschettaVmStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const BruschettaVmStartupRequest& request,
         BruschettaVmStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->BruschettaVmStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));

  if (!result) {
    LOG(ERROR) << __func__ << "(vm_id: " << vm_id
               << "): Bruschetta VM network startup failed: "
               << error->GetMessage();
    return std::nullopt;
  }

  const auto network_alloc = ConvertBruschettaAllocation(response);
  if (!network_alloc) {
    LOG(ERROR) << __func__ << "(vm_id: " << vm_id
               << "): Failed to convert Bruschetta VM network configuration";
  }
  return network_alloc;
}

bool ClientImpl::NotifyBruschettaVmShutdown(uint64_t vm_id) {
  BruschettaVmShutdownRequest request;
  request.set_id(vm_id);

  BruschettaVmShutdownResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const BruschettaVmShutdownRequest& request,
         BruschettaVmShutdownResponse* response, brillo::ErrorPtr* error) {
        return proxy->BruschettaVmShutdown(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "BruschettaVM network shutdown failed: "
               << error->GetMessage();
    return false;
  }
  return true;
}

std::optional<Client::BorealisAllocation> ClientImpl::NotifyBorealisVmStartup(
    uint32_t vm_id) {
  BorealisVmStartupRequest request;
  request.set_id(vm_id);

  BorealisVmStartupResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const BorealisVmStartupRequest& request,
         BorealisVmStartupResponse* response, brillo::ErrorPtr* error) {
        return proxy->BorealisVmStartup(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));

  if (!result) {
    LOG(ERROR) << __func__ << "(vm_id: " << vm_id
               << "): Borealis VM network startup failed: "
               << error->GetMessage();
    return std::nullopt;
  }

  const auto network_alloc = ConvertBorealisAllocation(response);
  if (!network_alloc) {
    LOG(ERROR) << __func__ << "(vm_id: " << vm_id
               << "): Failed to convert Borealis VM network configuration";
  }
  return network_alloc;
}

bool ClientImpl::NotifyBorealisVmShutdown(uint32_t vm_id) {
  BorealisVmShutdownRequest request;
  request.set_id(vm_id);

  BorealisVmShutdownResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const BorealisVmShutdownRequest& request,
         BorealisVmShutdownResponse* response, brillo::ErrorPtr* error) {
        return proxy->BorealisVmShutdown(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "Borealis VM network shutdown failed: "
               << error->GetMessage();
    return false;
  }
  return true;
}

std::pair<base::ScopedFD, Client::ConnectedNamespace>
ClientImpl::ConnectNamespace(pid_t pid,
                             const std::string& outbound_ifname,
                             bool forward_user_traffic,
                             bool route_on_vpn,
                             Client::TrafficSource traffic_source,
                             bool static_ipv6) {
  // Prepare and serialize the request proto.
  ConnectNamespaceRequest request;
  request.set_pid(static_cast<int32_t>(pid));
  request.set_outbound_physical_device(outbound_ifname);
  request.set_allow_user_traffic(forward_user_traffic);
  request.set_route_on_vpn(route_on_vpn);
  request.set_traffic_source(ConvertTrafficSource(traffic_source));
  request.set_static_ipv6(static_ipv6);

  auto [fd_local, fd_remote] = CreateLifelineFd();
  if (!fd_local.is_valid()) {
    LOG(ERROR)
        << "Cannot send ConnectNamespace message to patchpanel: no lifeline fd";
    return {};
  }

  ConnectNamespaceResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const ConnectNamespaceRequest& request, base::ScopedFD fd_remote,
         ConnectNamespaceResponse* response, brillo::ErrorPtr* error) {
        return proxy->ConnectNamespace(request, fd_remote, response, error);
      },
      pp_proxy_.get(), request, std::move(fd_remote), &response, &error));
  if (!result) {
    LOG(ERROR) << "ConnectNamespace failed: " << error->GetMessage();
    return {};
  }

  if (response.peer_ifname().empty() || response.host_ifname().empty()) {
    LOG(ERROR) << "ConnectNamespace for netns pid " << pid << " failed";
    return {};
  }

  const auto connected_ns = ConvertConnectedNamespace(response);
  if (!connected_ns) {
    LOG(ERROR) << "Failed to convert ConnectedNamespace";
    return {};
  }

  LOG(INFO) << "ConnectNamespace for netns pid " << pid
            << " succeeded: peer_ifname=" << connected_ns->peer_ifname
            << " peer_ipv4_address=" << connected_ns->peer_ipv4_address
            << " host_ifname=" << connected_ns->host_ifname
            << " host_ipv4_address=" << connected_ns->host_ipv4_address
            << " subnet=" << connected_ns->ipv4_subnet.ToString();

  return std::make_pair(std::move(fd_local), *connected_ns);
}

void ClientImpl::GetTrafficCounters(const std::set<std::string>& devices,
                                    GetTrafficCountersCallback callback) {
  TrafficCountersRequest request;
  for (const auto& device : devices) {
    request.add_devices(device);
  }

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const TrafficCountersRequest& request,
         GetTrafficCountersCallback callback) {
        auto split_callback = SplitOnceCallback(std::move(callback));
        proxy->GetTrafficCountersAsync(
            request,
            base::BindOnce(&OnGetTrafficCountersDBusResponse,
                           std::move(split_callback.first)),
            base::BindOnce(&OnGetTrafficCountersError,
                           std::move(split_callback.second)));
      },
      pp_proxy_.get(), request,
      base::BindPostTaskToCurrentDefault(std::move(callback))));
}

bool ClientImpl::ModifyPortRule(Client::FirewallRequestOperation op,
                                Client::FirewallRequestType type,
                                Client::FirewallRequestProtocol proto,
                                const std::string& input_ifname,
                                const std::string& input_dst_ip,
                                uint32_t input_dst_port,
                                const std::string& dst_ip,
                                uint32_t dst_port) {
  ModifyPortRuleRequest request;
  request.set_op(ConvertFirewallRequestOperation(op));
  request.set_type(ConvertFirewallRequestType(type));
  request.set_proto(ConvertFirewallRequestProtocol(proto));
  request.set_input_ifname(input_ifname);
  request.set_input_dst_ip(input_dst_ip);
  request.set_input_dst_port(input_dst_port);
  request.set_dst_ip(dst_ip);
  request.set_dst_port(dst_port);

  // TODO(b/284797476): Switch permission_brokker to use the async DBus call.
  ModifyPortRuleResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const ModifyPortRuleRequest& request,
         ModifyPortRuleResponse* response, brillo::ErrorPtr* error) {
        return proxy->ModifyPortRule(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));
  if (!result) {
    LOG(ERROR) << "ModifyPortRule failed: " << error->GetMessage();
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "ModifyPortRuleRequest failed " << request;
    return false;
  }
  return true;
}

void ClientImpl::SetVpnLockdown(bool enable) {
  SetVpnLockdownRequest request;
  request.set_enable_vpn_lockdown(enable);

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const SetVpnLockdownRequest& request) {
        // This API doesn't return anything.
        auto success_callback = base::DoNothing();
        // The current use case do not care about failures. Leaving a log is
        // enough now.
        auto error_callback = [](brillo::Error* error) {
          LOG(ERROR) << "SetVpnLockdown failed: " << error->GetMessage();
        };
        proxy->SetVpnLockdownAsync(request, success_callback,
                                   base::BindOnce(error_callback));
      },
      pp_proxy_.get(), request));
}

base::ScopedFD ClientImpl::RedirectDns(
    Client::DnsRedirectionRequestType type,
    const std::string& input_ifname,
    const std::string& proxy_address,
    const std::vector<std::string>& nameservers,
    const std::string& host_ifname) {
  SetDnsRedirectionRuleRequest request;
  request.set_type(ConvertDnsRedirectionRequestType(type));
  request.set_input_ifname(input_ifname);
  request.set_proxy_address(proxy_address);
  request.set_host_ifname(host_ifname);
  for (const auto& nameserver : nameservers) {
    request.add_nameservers(nameserver);
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CreateLifelineFd();
  if (!fd_local.is_valid()) {
    LOG(ERROR) << "Cannot send SetDnsRedirectionRuleRequest message to "
                  "patchpanel: no lifeline fd";
    return {};
  }

  SetDnsRedirectionRuleResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const SetDnsRedirectionRuleRequest& request, base::ScopedFD fd_remote,
         SetDnsRedirectionRuleResponse* response, brillo::ErrorPtr* error) {
        return proxy->SetDnsRedirectionRule(request, fd_remote, response,
                                            error);
      },
      pp_proxy_.get(), request, std::move(fd_remote), &response, &error));
  if (!result) {
    LOG(ERROR) << "SetDnsRedirectionRule failed: " << error->GetMessage();
    return {};
  }

  if (!response.success()) {
    LOG(ERROR) << "SetDnsRedirectionRuleRequest failed " << request;
    return {};
  }
  return std::move(fd_local);
}

std::vector<Client::VirtualDevice> ClientImpl::GetDevices() {
  // TODO(b/284797476): Add a DBus service in dns-proxy to let patchpanel push
  // information to dns-proxy.
  GetDevicesResponse response;
  brillo::ErrorPtr error;
  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, GetDevicesResponse* response,
         brillo::ErrorPtr* error) {
        return proxy->GetDevices({}, response, error);
      },
      pp_proxy_.get(), &response, &error));
  if (!result) {
    LOG(ERROR) << "GetDevices failed: " << error->GetMessage();
    return {};
  }

  std::vector<Client::VirtualDevice> devices;
  for (const auto& d : response.devices()) {
    const auto device = ConvertVirtualDevice(d);
    if (device) {
      devices.push_back(*device);
    }
  }
  return devices;
}

void ClientImpl::RegisterVirtualDeviceEventHandler(
    VirtualDeviceEventHandler handler) {
  pp_proxy_->RegisterNetworkDeviceChangedSignalHandler(
      base::BindRepeating(OnNetworkDeviceChanged, std::move(handler)),
      base::BindOnce(OnSignalConnectedCallback));
}

void ClientImpl::RegisterNeighborReachabilityEventHandler(
    NeighborReachabilityEventHandler handler) {
  pp_proxy_->RegisterNeighborReachabilityEventSignalHandler(
      base::BindRepeating(OnNeighborReachabilityEvent, std::move(handler)),
      base::BindOnce(OnSignalConnectedCallback));
}

bool ClientImpl::CreateTetheredNetwork(
    const std::string& downstream_ifname,
    const std::string& upstream_ifname,
    const std::optional<DHCPOptions>& dhcp_options,
    const std::optional<UplinkIPv6Configuration>& uplink_ipv6_config,
    const std::optional<int>& mtu,
    CreateTetheredNetworkCallback callback) {
  TetheredNetworkRequest request;
  request.set_ifname(downstream_ifname);
  request.set_upstream_ifname(upstream_ifname);
  if (mtu) {
    request.set_mtu(*mtu);
  }
  if (dhcp_options.has_value()) {
    auto* ipv4_config = request.mutable_ipv4_config();
    ipv4_config->set_use_dhcp(true);
    for (const auto& dns_server : dhcp_options->dns_server_addresses) {
      ipv4_config->add_dns_servers(dns_server.ToByteString());
    }
    for (const auto& domain_search : dhcp_options->domain_search_list) {
      ipv4_config->add_domain_searches(domain_search);
    }
    if (dhcp_options->is_android_metered) {
      auto options = ipv4_config->add_options();
      // RFC 3925 defines the DHCP option 43 is Vendor Specific.
      options->set_code(43);
      options->set_content("ANDROID_METERED");
    }
  }
  request.set_enable_ipv6(true);
  if (uplink_ipv6_config.has_value()) {
    auto* ipv6_config = request.mutable_uplink_ipv6_config();
    auto* uplink_ipv6_cidr = ipv6_config->mutable_uplink_ipv6_cidr();
    uplink_ipv6_cidr->set_addr(
        uplink_ipv6_config->uplink_address.address().ToByteString());
    uplink_ipv6_cidr->set_prefix_len(
        uplink_ipv6_config->uplink_address.prefix_length());
    for (const auto& dns_server : uplink_ipv6_config->dns_server_addresses) {
      ipv6_config->add_dns_servers(dns_server.ToByteString());
    }
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CreateLifelineFd();
  if (!fd_local.is_valid()) {
    LOG(ERROR) << kCreateTetheredNetworkMethod << "(" << downstream_ifname
               << "," << upstream_ifname << "): Cannot create lifeline fds";
    return false;
  }

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const TetheredNetworkRequest& request,
         base::ScopedFD fd_local, base::ScopedFD fd_remote,
         CreateTetheredNetworkCallback callback) {
        auto split_callback = SplitOnceCallback(std::move(callback));
        proxy->CreateTetheredNetworkAsync(
            request, fd_remote,
            base::BindOnce(&OnTetheredNetworkResponse,
                           std::move(split_callback.first),
                           std::move(fd_local)),
            base::BindOnce(&OnTetheredNetworkError,
                           std::move(split_callback.second)));
      },
      pp_proxy_.get(), request, std::move(fd_local), std::move(fd_remote),
      base::BindPostTaskToCurrentDefault(std::move(callback))));

  return true;
}

bool ClientImpl::CreateLocalOnlyNetwork(
    const std::string& ifname, CreateLocalOnlyNetworkCallback callback) {
  LocalOnlyNetworkRequest request;
  request.set_ifname(ifname);
  auto* ipv4_config = request.mutable_ipv4_config();
  ipv4_config->set_use_dhcp(true);

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CreateLifelineFd();
  if (!fd_local.is_valid()) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod
               << ": Cannot create lifeline fds";
    return false;
  }

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const LocalOnlyNetworkRequest& request, base::ScopedFD fd_local,
         base::ScopedFD fd_remote, CreateLocalOnlyNetworkCallback callback) {
        auto split_callback = SplitOnceCallback(std::move(callback));
        proxy->CreateLocalOnlyNetworkAsync(
            request, fd_remote,
            base::BindOnce(&OnLocalOnlyNetworkResponse,
                           std::move(split_callback.first),
                           std::move(fd_local)),
            base::BindOnce(&OnLocalOnlyNetworkError,
                           std::move(split_callback.second)));
      },
      pp_proxy_.get(), request, std::move(fd_local), std::move(fd_remote),
      base::BindPostTaskToCurrentDefault(std::move(callback))));

  return true;
}

bool ClientImpl::GetDownstreamNetworkInfo(
    const std::string& ifname, GetDownstreamNetworkInfoCallback callback) {
  GetDownstreamNetworkInfoRequest request;
  request.set_downstream_ifname(ifname);

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const GetDownstreamNetworkInfoRequest& request,
         GetDownstreamNetworkInfoCallback callback) {
        auto split_callback = SplitOnceCallback(std::move(callback));
        proxy->GetDownstreamNetworkInfoAsync(
            request,
            base::BindOnce(&OnGetDownstreamNetworkInfoResponse,
                           std::move(split_callback.first)),
            base::BindOnce(&OnGetDownstreamNetworkInfoError,
                           std::move(split_callback.second)));
      },
      pp_proxy_.get(), request,
      base::BindPostTaskToCurrentDefault(std::move(callback))));

  return true;
}

bool ClientImpl::ConfigureNetwork(int interface_index,
                                  std::string_view interface_name,
                                  uint32_t area,
                                  const net_base::NetworkConfig& network_config,
                                  net_base::NetworkPriority priority,
                                  NetworkTechnology technology,
                                  int session_id,
                                  ConfigureNetworkCallback callback) {
  ConfigureNetworkRequest request;
  request.set_technology(ConvertNetworkTechnology(technology));
  request.set_ifindex(interface_index);
  request.set_ifname(std::string(interface_name));
  request.set_area(area);
  auto request_priority = request.mutable_priority();
  request_priority->set_is_primary_logical(priority.is_primary_logical);
  request_priority->set_is_primary_physical(priority.is_primary_physical);
  request_priority->set_is_primary_for_dns(priority.is_primary_for_dns);
  request_priority->set_ranking_order(priority.ranking_order);
  request.set_session_id(session_id);

  SerializeNetworkConfig(network_config, request.mutable_network_config());

  RunOnDBusThreadAsync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy,
         const ConfigureNetworkRequest& request,
         std::string_view interface_name, ConfigureNetworkCallback callback) {
        auto split_callback = SplitOnceCallback(std::move(callback));
        proxy->ConfigureNetworkAsync(
            request,
            base::BindOnce(&OnConfigureNetworkResponse,
                           std::move(split_callback.first),
                           std::string(interface_name)),
            base::BindOnce(&OnConfigureNetworkError,
                           std::move(split_callback.second),
                           std::string(interface_name)));
      },
      pp_proxy_.get(), request, interface_name,
      base::BindPostTaskToCurrentDefault(std::move(callback))));
  return true;
}

bool ClientImpl::SendSetFeatureFlagRequest(FeatureFlag flag, bool enable) {
  SetFeatureFlagRequest request;
  request.set_enabled(enable);
  request.set_flag(ConvertFeatureFlag(flag));

  SetFeatureFlagResponse response;
  brillo::ErrorPtr error;

  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](PatchPanelProxyInterface* proxy, const SetFeatureFlagRequest& request,
         SetFeatureFlagResponse* response, brillo::ErrorPtr* error) {
        return proxy->SetFeatureFlag(request, response, error);
      },
      pp_proxy_.get(), request, &response, &error));

  if (!result) {
    LOG(ERROR) << "Failed to set feature flag of " << flag << ": "
               << error->GetMessage();
    return false;
  }

  return true;
}

bool ClientImpl::TagSocket(
    base::ScopedFD fd,
    std::optional<int> network_id,
    std::optional<VpnRoutingPolicy> vpn_policy,
    std::optional<TrafficAnnotation> traffic_annotation) {
  TagSocketRequest request;
  if (network_id.has_value()) {
    request.set_network_id(network_id.value());
  }
  if (vpn_policy.has_value()) {
    request.set_vpn_policy(ConvertVpnRoutingPolicy(vpn_policy.value()));
  }
  if (traffic_annotation.has_value()) {
    auto annotation = request.mutable_traffic_annotation();
    annotation->set_host_id(
        ConvertTrafficAnnotationId(traffic_annotation.value().id));
  }

  TagSocketResponse response;
  brillo::ErrorPtr error;

  const bool result = RunOnDBusThreadSync(base::BindOnce(
      [](SocketServiceProxyInterface* proxy, const TagSocketRequest& request,
         base::ScopedFD fd, TagSocketResponse* response,
         brillo::ErrorPtr* error) {
        return proxy->TagSocket(request, std::move(fd), response, error);
      },
      ss_proxy_.get(), request, std::move(fd), &response, &error));

  if (!result) {
    LOG(ERROR) << "Failed to tag socket";
    return false;
  }

  return true;
}

bool OnSocketAnnotation(base::WeakPtr<ClientImpl> client,
                        const Client::TrafficAnnotationId& id,
                        int fd) {
  // The callback might be still registered in the transport while the client
  // has been destroyed. Ensure the client is still valid before doing anything
  // else (see b/345769752).
  if (!client) {
    LOG(WARNING) << __func__ << ": client is not valid anymore";
    return false;
  }

  // The socket fd has to be duplicated to prevent the DBus proxy base::ScopedFD
  // to close the fd owned by curl.
  int tag_fd = dup(fd);
  if (tag_fd < 0) {
    LOG(ERROR) << __func__ << ": failed to dup socket descriptor";
    return false;
  }

  Client::TrafficAnnotation annotation(id);
  return client->TagSocket(base::ScopedFD(tag_fd), std::nullopt, std::nullopt,
                           std::move(annotation));
}

void ClientImpl::PrepareTagSocket(
    const TrafficAnnotation& annotation,
    std::shared_ptr<brillo::http::Transport> transport) {
  // Bind OnSocketAnnotation as a RepeatingCallback with the annotation id as a
  // bound parameter.
  transport->SetSockOptCallback(base::BindRepeating(
      &OnSocketAnnotation, weak_factory_.GetWeakPtr(), annotation.id));
}

}  // namespace

void SerializeNetworkConfig(const net_base::NetworkConfig& in,
                            patchpanel::NetworkConfig* out) {
  if (in.ipv4_address) {
    auto* ipv4_address = out->mutable_ipv4_address();
    ipv4_address->set_addr(in.ipv4_address->address().ToByteString());
    ipv4_address->set_prefix_len(in.ipv4_address->prefix_length());
  }
  if (in.ipv4_broadcast) {
    out->set_ipv4_broadcast(in.ipv4_broadcast->ToByteString());
  }
  if (in.ipv4_gateway) {
    out->set_ipv4_gateway(in.ipv4_gateway->ToByteString());
  }

  for (const auto& ipv6_address : in.ipv6_addresses) {
    auto* out_addr = out->add_ipv6_addresses();
    out_addr->set_addr(ipv6_address.address().ToByteString());
    out_addr->set_prefix_len(ipv6_address.prefix_length());
  }
  if (in.ipv6_gateway) {
    out->set_ipv6_gateway(in.ipv6_gateway->ToByteString());
  }
  for (const auto& ipv6_pd : in.ipv6_delegated_prefixes) {
    auto* out_pd = out->add_ipv6_delegated_prefixes();
    out_pd->set_addr(ipv6_pd.address().ToByteString());
    out_pd->set_prefix_len(ipv6_pd.prefix_length());
  }

  out->set_ipv6_blackhole_route(in.ipv6_blackhole_route);

  for (const auto& prefix : in.excluded_route_prefixes) {
    auto* out_prefix = out->add_excluded_route_prefixes();
    out_prefix->set_addr(prefix.address().ToByteString());
    out_prefix->set_prefix_len(prefix.prefix_length());
  }
  for (const auto& prefix : in.included_route_prefixes) {
    auto* out_prefix = out->add_included_route_prefixes();
    out_prefix->set_addr(prefix.address().ToByteString());
    out_prefix->set_prefix_len(prefix.prefix_length());
  }
  for (const auto& route : in.rfc3442_routes) {
    auto* out_route = out->add_rfc3442_routes();
    auto* out_prefix = out_route->mutable_prefix();
    out_prefix->set_addr(route.first.address().ToByteString());
    out_prefix->set_prefix_len(route.first.prefix_length());
    out_route->set_gateway(route.second.ToByteString());
  }

  for (const auto& dns : in.dns_servers) {
    out->add_dns_servers(dns.ToByteString());
  }
  for (const auto& dnssl : in.dns_search_domains) {
    out->add_dns_search_domains(dnssl);
  }
  if (in.mtu) {
    out->set_mtu(*in.mtu);
  }

  if (in.captive_portal_uri) {
    out->set_captive_portal_uri(in.captive_portal_uri->ToString());
  }
}

// static
std::unique_ptr<Client> Client::New() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return nullptr;
  }

  auto pp_proxy = std::make_unique<org::chromium::PatchPanelProxy>(bus);
  if (!pp_proxy) {
    LOG(ERROR) << "Failed to create proxy";
    return nullptr;
  }

  auto ss_proxy = std::make_unique<org::chromium::SocketServiceProxy>(bus);
  if (!ss_proxy) {
    LOG(ERROR) << "Failed to create SocketService proxy";
    return nullptr;
  }

  return std::make_unique<ClientImpl>(std::move(bus), std::move(pp_proxy),
                                      std::move(ss_proxy),
                                      /*owns_bus=*/true);
}

// static
std::unique_ptr<Client> Client::New(const scoped_refptr<dbus::Bus>& bus) {
  auto pp_proxy = std::make_unique<org::chromium::PatchPanelProxy>(bus);
  if (!pp_proxy) {
    LOG(ERROR) << "Failed to create proxy";
    return nullptr;
  }
  auto ss_proxy = std::make_unique<org::chromium::SocketServiceProxy>(bus);
  if (!ss_proxy) {
    LOG(ERROR) << "Failed to create SocketService proxy";
    return nullptr;
  }
  return std::make_unique<ClientImpl>(bus, std::move(pp_proxy),
                                      std::move(ss_proxy),
                                      /*owns_bus=*/false);
}

// static
std::unique_ptr<Client> Client::NewForTesting(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<org::chromium::PatchPanelProxyInterface> pp_proxy,
    std::unique_ptr<org::chromium::SocketServiceProxyInterface> ss_proxy) {
  return std::make_unique<ClientImpl>(std::move(bus), std::move(pp_proxy),
                                      std::move(ss_proxy),
                                      /*owns_bus=*/false);
}

// static
bool Client::IsArcGuest(Client::GuestType guest_type) {
  switch (guest_type) {
    case Client::GuestType::kArcContainer:
    case Client::GuestType::kArcVm:
      return true;
    default:
      return false;
  }
}

// static
std::string Client::TrafficSourceName(
    patchpanel::Client::TrafficSource source) {
  return patchpanel::TrafficCounter::Source_Name(ConvertTrafficSource(source));
}

// static
std::string Client::ProtocolName(
    patchpanel::Client::FirewallRequestProtocol protocol) {
  return patchpanel::ModifyPortRuleRequest::Protocol_Name(
      ConvertFirewallRequestProtocol(protocol));
}

// static
std::string Client::NeighborRoleName(patchpanel::Client::NeighborRole role) {
  return NeighborReachabilityEventSignal::Role_Name(ConvertNeighborRole(role));
}

// static
std::string Client::NeighborStatusName(
    patchpanel::Client::NeighborStatus status) {
  return NeighborReachabilityEventSignal::EventType_Name(
      ConvertNeighborStatus(status));
}

bool Client::TrafficVector::operator==(
    const Client::TrafficVector& that) const = default;

Client::TrafficVector& Client::TrafficVector::operator+=(
    const TrafficVector& that) {
  rx_bytes += that.rx_bytes;
  tx_bytes += that.tx_bytes;
  rx_packets += that.rx_packets;
  tx_packets += that.tx_packets;
  return *this;
}

Client::TrafficVector& Client::TrafficVector::operator-=(
    const TrafficVector& that) {
  rx_bytes -= that.rx_bytes;
  tx_bytes -= that.tx_bytes;
  rx_packets -= that.rx_packets;
  tx_packets -= that.tx_packets;
  return *this;
}

Client::TrafficVector Client::TrafficVector::operator+(
    const TrafficVector& that) const {
  auto r = *this;
  r += that;
  return r;
}

Client::TrafficVector Client::TrafficVector::operator-(
    const TrafficVector& that) const {
  auto r = *this;
  r -= that;
  return r;
}

Client::TrafficVector Client::TrafficVector::operator-() const {
  auto r = *this;
  r.rx_bytes = -r.rx_bytes;
  r.tx_bytes = -r.tx_bytes;
  r.rx_packets = -r.rx_packets;
  r.tx_packets = -r.tx_packets;
  return r;
}

bool Client::TrafficCounter::operator==(
    const Client::TrafficCounter& rhs) const = default;

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::NeighborReachabilityEvent& event) {
  return stream << "{ifindex: " << event.ifindex
                << ", ip_address: " << event.ip_addr
                << ", role: " << Client::NeighborRoleName(event.role)
                << ", status: " << Client::NeighborStatusName(event.status)
                << "}";
}

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::NetworkTechnology& technology) {
  switch (technology) {
    case Client::NetworkTechnology::kCellular:
      return stream << "Cellular";
    case Client::NetworkTechnology::kEthernet:
      return stream << "Ethernet";
    case Client::NetworkTechnology::kVPN:
      return stream << "VPN";
    case Client::NetworkTechnology::kWiFi:
      return stream << "WiFi";
  }
}

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::TrafficSource& traffic_source) {
  switch (traffic_source) {
    case patchpanel::Client::TrafficSource::kUnknown:
      return stream << "Unknown";
    case patchpanel::Client::TrafficSource::kChrome:
      return stream << "Chrome";
    case patchpanel::Client::TrafficSource::kUser:
      return stream << "User";
    case patchpanel::Client::TrafficSource::kUpdateEngine:
      return stream << "UE";
    case patchpanel::Client::TrafficSource::kSystem:
      return stream << "System";
    case patchpanel::Client::TrafficSource::kVpn:
      return stream << "VPN";
    case patchpanel::Client::TrafficSource::kArc:
      return stream << "ARC";
    case patchpanel::Client::TrafficSource::kBorealisVM:
      return stream << "Borealis";
    case patchpanel::Client::TrafficSource::kBruschettaVM:
      return stream << "Bruschetta";
    case patchpanel::Client::TrafficSource::kCrostiniVM:
      return stream << "Crostini";
    case patchpanel::Client::TrafficSource::kParallelsVM:
      return stream << "Parallels";
    case patchpanel::Client::TrafficSource::kTethering:
      return stream << "Tethering";
    case patchpanel::Client::TrafficSource::kWiFiDirect:
      return stream << "WiFi Direct";
    case patchpanel::Client::TrafficSource::kWiFiLOHS:
      return stream << "WiFi LOHS";
  }
}

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::TrafficVector& traffic_vector) {
  return stream << "[rx=" << traffic_vector.rx_bytes
                << ", tx=" << traffic_vector.tx_bytes << "]";
}

}  // namespace patchpanel
