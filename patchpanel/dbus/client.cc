// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <fcntl.h>
#include <string.h>

#include <algorithm>
#include <optional>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_util.h>
#include <brillo/dbus/dbus_proxy_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

void CopyBytes(const std::string& from, std::vector<uint8_t>* to) {
  to->assign(from.begin(), from.end());
}

patchpanel::TrafficCounter::Source ConvertTrafficSource(
    Client::TrafficSource source) {
  switch (source) {
    case Client::TrafficSource::kUnknown:
      return patchpanel::TrafficCounter::UNKNOWN;
    case Client::TrafficSource::kChrome:
      return patchpanel::TrafficCounter::CHROME;
    case Client::TrafficSource::kUser:
      return patchpanel::TrafficCounter::USER;
    case Client::TrafficSource::kArc:
      return patchpanel::TrafficCounter::ARC;
    case Client::TrafficSource::kCrosVm:
      return patchpanel::TrafficCounter::CROSVM;
    case Client::TrafficSource::kPluginVm:
      return patchpanel::TrafficCounter::PLUGINVM;
    case Client::TrafficSource::kUpdateEngine:
      return patchpanel::TrafficCounter::UPDATE_ENGINE;
    case Client::TrafficSource::kVpn:
      return patchpanel::TrafficCounter::VPN;
    case Client::TrafficSource::kSystem:
      return patchpanel::TrafficCounter::SYSTEM;
  }
}

Client::TrafficSource ConvertTrafficSource(
    patchpanel::TrafficCounter::Source source) {
  switch (source) {
    case patchpanel::TrafficCounter::CHROME:
      return Client::TrafficSource::kChrome;
    case patchpanel::TrafficCounter::USER:
      return Client::TrafficSource::kUser;
    case patchpanel::TrafficCounter::ARC:
      return Client::TrafficSource::kArc;
    case patchpanel::TrafficCounter::CROSVM:
      return Client::TrafficSource::kCrosVm;
    case patchpanel::TrafficCounter::PLUGINVM:
      return Client::TrafficSource::kPluginVm;
    case patchpanel::TrafficCounter::UPDATE_ENGINE:
      return Client::TrafficSource::kUpdateEngine;
    case patchpanel::TrafficCounter::VPN:
      return Client::TrafficSource::kVpn;
    case patchpanel::TrafficCounter::SYSTEM:
      return Client::TrafficSource::kSystem;
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

std::vector<uint8_t> ConvertIPv4Addr(uint32_t in) {
  std::vector<uint8_t> out;
  out.resize(4);
  memcpy(out.data(), &in, sizeof(in));
  return out;
}

Client::IPv4Subnet ConvertIPv4Subnet(const IPv4Subnet& in) {
  Client::IPv4Subnet out = {};
  out.base_addr.assign(in.addr().begin(), in.addr().begin());
  CopyBytes(in.addr(), &out.base_addr);
  out.prefix_len = static_cast<int>(in.prefix_len());
  return out;
}

std::optional<Client::TrafficCounter> ConvertTrafficCounter(
    const TrafficCounter& in) {
  auto out = std::make_optional<Client::TrafficCounter>();
  out->rx_bytes = in.rx_bytes();
  out->tx_bytes = in.tx_bytes();
  out->rx_packets = in.rx_packets();
  out->tx_packets = in.tx_packets();
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
  out->ipv4_addr = ConvertIPv4Addr(in.ipv4_addr());
  out->host_ipv4_addr = ConvertIPv4Addr(in.host_ipv4_addr());
  out->ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  CopyBytes(in.dns_proxy_ipv4_addr(), &out->dns_proxy_ipv4_addr);
  CopyBytes(in.dns_proxy_ipv6_addr(), &out->dns_proxy_ipv6_addr);
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
    case patchpanel::NetworkDevice::PLUGIN_VM:
      out->guest_type = Client::GuestType::kPluginVm;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown GuestType "
                 << patchpanel::NetworkDevice::GuestType_Name(in.guest_type());
      return std::nullopt;
  }
  return out;
}

Client::NetworkClientInfo ConvertNetworkClientInfo(
    const NetworkClientInfo& in) {
  Client::NetworkClientInfo out;
  std::copy(in.mac_addr().begin(), in.mac_addr().end(),
            std::back_inserter(out.mac_addr));
  std::copy(in.ipv4_addr().begin(), in.ipv4_addr().end(),
            std::back_inserter(out.ipv4_addr));
  for (const auto& ipv6_addr : in.ipv6_addresses()) {
    out.ipv6_addresses.emplace_back(ipv6_addr.begin(), ipv6_addr.end());
  }
  out.hostname = in.hostname();
  out.vendor_class = in.vendor_class();
  return out;
}

Client::DownstreamNetwork ConvertDownstreamNetwork(
    const DownstreamNetwork& in) {
  Client::DownstreamNetwork out;
  out.ifname = in.downstream_ifname();
  out.ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  CopyBytes(in.ipv4_gateway_addr(), &out.ipv4_gateway_addr);
  return out;
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

Client::ConnectedNamespace ConvertConnectedNamespace(
    const ConnectNamespaceResponse& in) {
  Client::ConnectedNamespace out;
  out.ipv4_subnet = ConvertIPv4Subnet(in.ipv4_subnet());
  out.peer_ifname = in.peer_ifname();
  out.peer_ipv4_address = ConvertIPv4Addr(in.peer_ipv4_address());
  out.host_ifname = in.host_ifname();
  out.host_ipv4_address = ConvertIPv4Addr(in.host_ipv4_address());
  out.netns_name = in.netns_name();
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

// Prepares a pair of ScopedFDs corresponding to the write end (pair first
// element) and read end (pair second lemenet) of a Linux pipe and appends the
// read end to the given |writer| to send to patchpanel. The client must keep
// the read end alive until the DBus request is successfully sent. The client
// must keep the write end alive until the setup requested from patchpanel is
// not necessary anymore.
std::pair<base::ScopedFD, base::ScopedFD> CommitLifelineFd(
    dbus::MessageWriter* writer) {
  int pipe_fds[2] = {-1, -1};
  if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
    PLOG(ERROR) << "Failed to create a pair of fds with pipe2()";
    return {};
  }
  // MessageWriter::AppendFileDescriptor duplicates the fd, so the original read
  // fd is given back to the caller using ScopedFD to make sure the it is
  // eventually closed.
  writer->AppendFileDescriptor(pipe_fds[1]);
  return {base::ScopedFD(pipe_fds[0]), base::ScopedFD(pipe_fds[1])};
}

void OnGetTrafficCountersDBusResponse(
    Client::GetTrafficCountersCallback callback,
    dbus::Response* dbus_response) {
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send TrafficCountersRequest message to patchpanel "
                  "service";
    std::move(callback).Run({});
    return;
  }

  TrafficCountersResponse response;
  dbus::MessageReader reader(dbus_response);
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse TrafficCountersResponse proto";
    std::move(callback).Run({});
    return;
  }

  std::vector<Client::TrafficCounter> counters;
  for (const auto& proto_counter : response.counters()) {
    auto client_counter = ConvertTrafficCounter(proto_counter);
    if (client_counter) {
      counters.push_back(*client_counter);
    }
  }
  std::move(callback).Run(counters);
}

void OnNetworkDeviceChangedSignal(
    const Client::VirtualDeviceEventHandler& handler, dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  NetworkDeviceChangedSignal proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Failed to parse NetworkDeviceChangedSignal proto";
    return;
  }

  const auto event = ConvertVirtualDeviceEvent(proto);
  if (!event) {
    return;
  }
  const auto device = ConvertVirtualDevice(proto.device());
  if (device) {
    handler.Run(*event, *device);
  }
}

void OnNeighborReachabilityEventSignal(
    const Client::NeighborReachabilityEventHandler& handler,
    dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  NeighborReachabilityEventSignal proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Failed to parse NeighborConnectedStateChangedSignal proto";
    return;
  }

  const auto event = ConvertNeighborReachabilityEvent(proto);
  if (event) {
    handler.Run(*event);
  }
}

void OnSignalConnectedCallback(const std::string& interface_name,
                               const std::string& signal_name,
                               bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to " << signal_name;
}

// Helper static function to process answers to CreateTetheredNetwork calls.
void OnTetheredNetworkResponse(Client::CreateTetheredNetworkCallback callback,
                               base::ScopedFD fd_local,
                               dbus::Response* dbus_response) {
  if (!dbus_response) {
    LOG(ERROR)
        << kCreateTetheredNetworkMethod
        << ": Failed to send TetheredNetworkRequest message to patchpanel";
    std::move(callback).Run({});
    return;
  }

  TetheredNetworkResponse response;
  dbus::MessageReader reader(dbus_response);
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << kCreateTetheredNetworkMethod
               << ": Failed to parse TetheredNetworkResponse proto";
    std::move(callback).Run({});
    return;
  }

  if (response.response_code() != DownstreamNetworkResult::SUCCESS) {
    LOG(ERROR) << kCreateTetheredNetworkMethod << " failed: "
               << patchpanel::DownstreamNetworkResult_Name(
                      response.response_code());
    std::move(callback).Run({});
    return;
  }

  std::move(callback).Run(std::move(fd_local));
}

// Helper static function to process answers to CreateLocalOnlyNetwork calls.
void OnLocalOnlyNetworkResponse(Client::CreateLocalOnlyNetworkCallback callback,
                                base::ScopedFD fd_local,
                                dbus::Response* dbus_response) {
  if (!dbus_response) {
    LOG(ERROR)
        << kCreateLocalOnlyNetworkMethod
        << ": Failed to send LocalOnlyNetworkRequest message to patchpanel";
    std::move(callback).Run({});
    return;
  }

  LocalOnlyNetworkResponse response;
  dbus::MessageReader reader(dbus_response);
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod
               << ": Failed to parse LocalOnlyNetworkResponse proto";
    std::move(callback).Run({});
    return;
  }

  if (response.response_code() != DownstreamNetworkResult::SUCCESS) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod << " failed: "
               << patchpanel::DownstreamNetworkResult_Name(
                      response.response_code());
    std::move(callback).Run({});
    return;
  }

  std::move(callback).Run(std::move(fd_local));
}

// Helper static function to process answers to DownstreamNetworkInfo calls.
void OnDownstreamNetworkInfoResponse(
    Client::DownstreamNetworkInfoCallback callback,
    dbus::Response* dbus_response) {
  if (!dbus_response) {
    LOG(ERROR) << kDownstreamNetworkInfoMethod
               << ": Failed to send DownstreamNetworkInfoRequest message to "
                  "patchpanel";
    std::move(callback).Run(false, {}, {});
    return;
  }

  DownstreamNetworkInfoResponse response;
  dbus::MessageReader reader(dbus_response);
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << kDownstreamNetworkInfoMethod
               << ": Failed to parse DownstreamNetworkInfoResponse proto";
    std::move(callback).Run(false, {}, {});
    return;
  }

  auto downstream_network =
      ConvertDownstreamNetwork(response.downstream_network());

  std::vector<Client::NetworkClientInfo> clients_info;
  for (const auto& ci : response.clients_info()) {
    clients_info.push_back(ConvertNetworkClientInfo(ci));
  }

  std::move(callback).Run(true, downstream_network, clients_info);
}

class ClientImpl : public Client {
 public:
  ClientImpl(const scoped_refptr<dbus::Bus>& bus,
             dbus::ObjectProxy* proxy,
             bool owns_bus)
      : bus_(std::move(bus)), proxy_(proxy), owns_bus_(owns_bus) {}
  ClientImpl(const ClientImpl&) = delete;
  ClientImpl& operator=(const ClientImpl&) = delete;

  ~ClientImpl();

  void RegisterOnAvailableCallback(
      base::RepeatingCallback<void(bool)> callback) override;

  void RegisterProcessChangedCallback(
      base::RepeatingCallback<void(bool)> callback) override;

  bool NotifyArcStartup(pid_t pid) override;
  bool NotifyArcShutdown() override;

  std::vector<Client::VirtualDevice> NotifyArcVmStartup(uint32_t cid) override;
  bool NotifyArcVmShutdown(uint32_t cid) override;

  bool NotifyTerminaVmStartup(uint32_t cid,
                              Client::VirtualDevice* device,
                              Client::IPv4Subnet* container_subnet) override;
  bool NotifyTerminaVmShutdown(uint32_t cid) override;

  bool NotifyPluginVmStartup(uint64_t vm_id,
                             int subnet_index,
                             Client::VirtualDevice* device) override;
  bool NotifyPluginVmShutdown(uint64_t vm_id) override;

  bool DefaultVpnRouting(int socket) override;

  bool RouteOnVpn(int socket) override;

  bool BypassVpn(int socket) override;

  std::pair<base::ScopedFD, Client::ConnectedNamespace> ConnectNamespace(
      pid_t pid,
      const std::string& outbound_ifname,
      bool forward_user_traffic,
      bool route_on_vpn,
      Client::TrafficSource traffic_source) override;

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

  bool SetVpnLockdown(bool enable) override;

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

  bool CreateTetheredNetwork(const std::string& downstream_ifname,
                             const std::string& upstream_ifname,
                             CreateTetheredNetworkCallback callback) override;

  bool CreateLocalOnlyNetwork(const std::string& ifname,
                              CreateLocalOnlyNetworkCallback callback) override;

  bool GetDownstreamNetworkInfo(
      const std::string& ifname,
      DownstreamNetworkInfoCallback callback) override;

 private:
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_ = nullptr;  // owned by |bus_|
  bool owns_bus_;  // Yes if |bus_| is created by Client::New

  base::RepeatingCallback<void(bool)> owner_callback_;

  void OnOwnerChanged(const std::string& old_owner,
                      const std::string& new_owner);

  bool SendSetVpnIntentRequest(int socket,
                               SetVpnIntentRequest::VpnRoutingPolicy policy);

  base::WeakPtrFactory<ClientImpl> weak_factory_{this};
};

ClientImpl::~ClientImpl() {
  if (bus_ && owns_bus_)
    bus_->ShutdownAndBlock();
}

void ClientImpl::RegisterOnAvailableCallback(
    base::RepeatingCallback<void(bool)> callback) {
  if (!proxy_) {
    LOG(ERROR) << "Cannot register callback - no proxy";
    return;
  }
  proxy_->WaitForServiceToBeAvailable(callback);
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
    if (!owner_callback_.is_null())
      owner_callback_.Run(false);
    return;
  }

  LOG(INFO) << "Patchpanel reset";
  if (!owner_callback_.is_null())
    owner_callback_.Run(true);
}

bool ClientImpl::NotifyArcStartup(pid_t pid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcStartupRequest request;
  request.set_pid(pid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyArcShutdown() {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcShutdownRequest request;
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

std::vector<Client::VirtualDevice> ClientImpl::NotifyArcVmStartup(
    uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmStartupRequest proto";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
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

bool ClientImpl::NotifyArcVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyTerminaVmStartup(uint32_t cid,
                                        Client::VirtualDevice* device,
                                        Client::IPv4Subnet* container_subnet) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No virtual device found";
    return false;
  }

  const auto response_device = ConvertVirtualDevice(response.device());
  if (!response_device) {
    LOG(ERROR) << "Invalid virtual device response";
    return false;
  }
  *device = *response_device;

  if (response.has_container_subnet()) {
    *container_subnet = ConvertIPv4Subnet(response.container_subnet());
  } else {
    LOG(WARNING) << "No container subnet found";
  }

  return true;
}

bool ClientImpl::NotifyTerminaVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyPluginVmStartup(uint64_t vm_id,
                                       int subnet_index,
                                       Client::VirtualDevice* device) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmStartupRequest request;
  request.set_id(vm_id);
  request.set_subnet_index(subnet_index);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No virtual device found";
    return false;
  }

  const auto response_device = ConvertVirtualDevice(response.device());
  if (!response_device) {
    LOG(ERROR) << "Invalid virtual device response";
    return false;
  }

  *device = *response_device;
  return true;
}

bool ClientImpl::NotifyPluginVmShutdown(uint64_t vm_id) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmShutdownRequest request;
  request.set_id(vm_id);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::DefaultVpnRouting(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::DEFAULT_ROUTING);
}

bool ClientImpl::RouteOnVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::ROUTE_ON_VPN);
}

bool ClientImpl::BypassVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::BYPASS_VPN);
}

bool ClientImpl::SendSetVpnIntentRequest(
    int socket, SetVpnIntentRequest::VpnRoutingPolicy policy) {
  dbus::MethodCall method_call(kPatchPanelInterface, kSetVpnIntentMethod);
  dbus::MessageWriter writer(&method_call);

  SetVpnIntentRequest request;
  SetVpnIntentResponse response;
  request.set_policy(policy);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SetVpnIntentRequest proto";
    return false;
  }
  writer.AppendFileDescriptor(socket);

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to send SetVpnIntentRequest message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse SetVpnIntentResponse proto";
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "SetVpnIntentRequest failed";
    return false;
  }
  return true;
}

std::pair<base::ScopedFD, Client::ConnectedNamespace>
ClientImpl::ConnectNamespace(pid_t pid,
                             const std::string& outbound_ifname,
                             bool forward_user_traffic,
                             bool route_on_vpn,
                             Client::TrafficSource traffic_source) {
  // Prepare and serialize the request proto.
  ConnectNamespaceRequest request;
  request.set_pid(static_cast<int32_t>(pid));
  request.set_outbound_physical_device(outbound_ifname);
  request.set_allow_user_traffic(forward_user_traffic);
  request.set_route_on_vpn(route_on_vpn);
  request.set_traffic_source(ConvertTrafficSource(traffic_source));

  dbus::MethodCall method_call(kPatchPanelInterface, kConnectNamespaceMethod);
  dbus::MessageWriter writer(&method_call);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ConnectNamespaceRequest proto";
    return {};
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CommitLifelineFd(&writer);
  if (!fd_local.is_valid()) {
    LOG(ERROR)
        << "Cannot send ConnectNamespace message to patchpanel: no lifeline fd";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send ConnectNamespace message to patchpanel";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  ConnectNamespaceResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse ConnectNamespaceResponse proto";
    return {};
  }

  if (response.peer_ifname().empty() || response.host_ifname().empty()) {
    LOG(ERROR) << "ConnectNamespace for netns pid " << pid << " failed";
    return {};
  }

  auto connected_ns = ConvertConnectedNamespace(response);

  std::string subnet_info = IPv4AddressToCidrString(
      connected_ns.ipv4_subnet.base_addr, connected_ns.ipv4_subnet.prefix_len);
  LOG(INFO) << "ConnectNamespace for netns pid " << pid
            << " succeeded: peer_ifname=" << connected_ns.peer_ifname
            << " peer_ipv4_address="
            << IPv4AddressToString(connected_ns.peer_ipv4_address)
            << " host_ifname=" << connected_ns.host_ifname
            << " host_ipv4_address="
            << IPv4AddressToString(connected_ns.host_ipv4_address)
            << " subnet=" << subnet_info;

  return std::make_pair(std::move(fd_local), std::move(connected_ns));
}

void ClientImpl::GetTrafficCounters(const std::set<std::string>& devices,
                                    GetTrafficCountersCallback callback) {
  dbus::MethodCall method_call(kPatchPanelInterface, kGetTrafficCountersMethod);
  dbus::MessageWriter writer(&method_call);

  TrafficCountersRequest request;
  for (const auto& device : devices) {
    request.add_devices(device);
  }

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TrafficCountersRequest proto";
    std::move(callback).Run({});
    return;
  }

  proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&OnGetTrafficCountersDBusResponse, std::move(callback)));
}

bool ClientImpl::ModifyPortRule(Client::FirewallRequestOperation op,
                                Client::FirewallRequestType type,
                                Client::FirewallRequestProtocol proto,
                                const std::string& input_ifname,
                                const std::string& input_dst_ip,
                                uint32_t input_dst_port,
                                const std::string& dst_ip,
                                uint32_t dst_port) {
  dbus::MethodCall method_call(kPatchPanelInterface, kModifyPortRuleMethod);
  dbus::MessageWriter writer(&method_call);

  ModifyPortRuleRequest request;
  ModifyPortRuleResponse response;

  request.set_op(ConvertFirewallRequestOperation(op));
  request.set_type(ConvertFirewallRequestType(type));
  request.set_proto(ConvertFirewallRequestProtocol(proto));
  request.set_input_ifname(input_ifname);
  request.set_input_dst_ip(input_dst_ip);
  request.set_input_dst_port(input_dst_port);
  request.set_dst_ip(dst_ip);
  request.set_dst_port(dst_port);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ModifyPortRuleRequest proto " << request;
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to send ModifyPortRuleRequest message to patchpanel service "
        << request;
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse ModifyPortRuleResponse proto " << request;
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "ModifyPortRuleRequest failed " << request;
    return false;
  }
  return true;
}

bool ClientImpl::SetVpnLockdown(bool enable) {
  dbus::MethodCall method_call(kPatchPanelInterface, kSetVpnLockdown);
  dbus::MessageWriter writer(&method_call);

  SetVpnLockdownRequest request;
  SetVpnLockdownResponse response;

  request.set_enable_vpn_lockdown(enable);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SetVpnLockdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to call SetVpnLockdown patchpanel API";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse SetVpnLockdownResponse";
    return false;
  }

  return true;
}

base::ScopedFD ClientImpl::RedirectDns(
    Client::DnsRedirectionRequestType type,
    const std::string& input_ifname,
    const std::string& proxy_address,
    const std::vector<std::string>& nameservers,
    const std::string& host_ifname) {
  dbus::MethodCall method_call(kPatchPanelInterface,
                               kSetDnsRedirectionRuleMethod);
  dbus::MessageWriter writer(&method_call);

  SetDnsRedirectionRuleRequest request;
  SetDnsRedirectionRuleResponse response;

  request.set_type(ConvertDnsRedirectionRequestType(type));
  request.set_input_ifname(input_ifname);
  request.set_proxy_address(proxy_address);
  request.set_host_ifname(host_ifname);
  for (const auto& nameserver : nameservers) {
    request.add_nameservers(nameserver);
  }

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SetDnsRedirectionRuleRequest proto "
               << request;
    return {};
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CommitLifelineFd(&writer);
  if (!fd_local.is_valid()) {
    LOG(ERROR) << "Cannot send SetDnsRedirectionRuleRequest message to "
                  "patchpanel: no lifeline fd";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send SetDnsRedirectionRuleRequest message to "
                  "patchpanel service "
               << request;
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse SetDnsRedirectionRuleResponse proto "
               << request;
    return {};
  }

  if (!response.success()) {
    LOG(ERROR) << "SetDnsRedirectionRuleRequest failed " << request;
    return {};
  }
  return std::move(fd_local);
}

std::vector<Client::VirtualDevice> ClientImpl::GetDevices() {
  dbus::MethodCall method_call(kPatchPanelInterface, kGetDevicesMethod);
  dbus::MessageWriter writer(&method_call);

  GetDevicesRequest request;
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode GetDevicesRequest proto";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethod(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  GetDevicesResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
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
  proxy_->ConnectToSignal(
      kPatchPanelInterface, kNetworkDeviceChangedSignal,
      base::BindRepeating(OnNetworkDeviceChangedSignal, handler),
      base::BindOnce(OnSignalConnectedCallback));
}

void ClientImpl::RegisterNeighborReachabilityEventHandler(
    NeighborReachabilityEventHandler handler) {
  proxy_->ConnectToSignal(
      kPatchPanelInterface, kNeighborReachabilityEventSignal,
      base::BindRepeating(OnNeighborReachabilityEventSignal, handler),
      base::BindOnce(OnSignalConnectedCallback));
}

bool ClientImpl::CreateTetheredNetwork(const std::string& downstream_ifname,
                                       const std::string& upstream_ifname,
                                       CreateTetheredNetworkCallback callback) {
  // TODO(b/275278561): Get the DNS server and domain search from the caller.
  const std::vector<std::array<uint8_t, 4>> dns_servers = {{8, 8, 8, 8}};
  const std::vector<std::string> domain_searches = {};

  dbus::MethodCall method_call(kPatchPanelInterface,
                               kCreateTetheredNetworkMethod);
  dbus::MessageWriter writer(&method_call);

  TetheredNetworkRequest request;
  request.set_ifname(downstream_ifname);
  request.set_upstream_ifname(upstream_ifname);
  // TODO(b/239559602) Fill out DHCP options:
  //  - If the upstream network has a DHCP lease, copy relevant options.
  //  - Option 43 with ANDROID_METERED if the upstream network is metered.
  //  - Forward DHCP WPAD proxy configuration if advertised by the upstream
  //    network.
  auto* ipv4_config = request.mutable_ipv4_config();
  ipv4_config->set_use_dhcp(true);
  for (const auto& dns_server : dns_servers) {
    ipv4_config->add_dns_servers(dns_server.data(), dns_server.size());
  }
  for (const auto& domain_search : domain_searches) {
    ipv4_config->add_domain_searches(domain_search);
  }

  request.set_enable_ipv6(true);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << kCreateTetheredNetworkMethod << "(" << downstream_ifname
               << "," << upstream_ifname
               << "): Failed to encode TetheredNetworkRequest proto";
    return false;
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CommitLifelineFd(&writer);
  if (!fd_local.is_valid()) {
    LOG(ERROR) << kCreateTetheredNetworkMethod << "(" << downstream_ifname
               << "," << upstream_ifname << "): Cannot create lifeline fds";
    return false;
  }

  proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::BindOnce(&OnTetheredNetworkResponse,
                                    std::move(callback), std::move(fd_local)));
  return true;
}

bool ClientImpl::CreateLocalOnlyNetwork(
    const std::string& ifname, CreateLocalOnlyNetworkCallback callback) {
  dbus::MethodCall method_call(kPatchPanelInterface,
                               kCreateLocalOnlyNetworkMethod);
  dbus::MessageWriter writer(&method_call);

  LocalOnlyNetworkRequest request;
  request.set_ifname(ifname);
  auto* ipv4_config = request.mutable_ipv4_config();
  ipv4_config->set_use_dhcp(true);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod
               << ": Failed to encode LocalOnlyNetworkRequest proto";
    return false;
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  auto [fd_local, fd_remote] = CommitLifelineFd(&writer);
  if (!fd_local.is_valid()) {
    LOG(ERROR) << kCreateLocalOnlyNetworkMethod
               << ": Cannot create lifeline fds";
    return false;
  }

  proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::BindOnce(&OnLocalOnlyNetworkResponse,
                                    std::move(callback), std::move(fd_local)));
  return true;
}

bool ClientImpl::GetDownstreamNetworkInfo(
    const std::string& ifname, DownstreamNetworkInfoCallback callback) {
  dbus::MethodCall method_call(kPatchPanelInterface,
                               kDownstreamNetworkInfoMethod);
  dbus::MessageWriter writer(&method_call);

  DownstreamNetworkInfoRequest request;
  request.set_downstream_ifname(ifname);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode DownstreamNetworkInfoRequest proto";
    return false;
  }

  proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&OnDownstreamNetworkInfoResponse, std::move(callback)));
  return true;
}

dbus::ObjectProxy* GetProxy(const scoped_refptr<dbus::Bus>& bus) {
  dbus::ObjectProxy* proxy = bus->GetObjectProxy(
      kPatchPanelServiceName, dbus::ObjectPath(kPatchPanelServicePath));
  if (!proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for " << kPatchPanelServiceName;
  }
  return proxy;
}

}  // namespace

// static
std::unique_ptr<Client> Client::New() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return nullptr;
  }

  dbus::ObjectProxy* proxy = GetProxy(bus);
  if (!proxy)
    return nullptr;

  return std::make_unique<ClientImpl>(std::move(bus), proxy,
                                      /*owns_bus=*/true);
}

std::unique_ptr<Client> Client::New(const scoped_refptr<dbus::Bus>& bus) {
  dbus::ObjectProxy* proxy = GetProxy(bus);
  if (!proxy)
    return nullptr;

  return std::make_unique<ClientImpl>(std::move(bus), proxy,
                                      /*owns_bus=*/false);
}

std::unique_ptr<Client> Client::New(const scoped_refptr<dbus::Bus>& bus,
                                    dbus::ObjectProxy* proxy) {
  return std::make_unique<ClientImpl>(std::move(bus), proxy,
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

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::NeighborReachabilityEvent& event) {
  return stream << "{ifindex: " << event.ifindex
                << ", ip_address: " << event.ip_addr
                << ", role: " << Client::NeighborRoleName(event.role)
                << ", status: " << Client::NeighborStatusName(event.status)
                << "}";
}

}  // namespace patchpanel
