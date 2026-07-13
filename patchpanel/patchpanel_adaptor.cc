// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/patchpanel_adaptor.h"

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/notreached.h>
#include <base/posix/eintr_wrapper.h>
#include <chromeos/dbus/patchpanel/dbus-constants.h>
#include <chromeos/net-base/process_manager.h>
#include <dbus/dbus-protocol.h>
#include <dbus/object_proxy.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <metrics/metrics_library.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/downstream_network_info.h"
#include "patchpanel/downstream_network_service.h"
#include "patchpanel/manager.h"
#include "patchpanel/metrics.h"
#include "patchpanel/network/network_applier.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/rtnl_client.h"

namespace patchpanel {
namespace {
// Convenience function to duplicate a ScopedFD.
base::ScopedFD Dup(const base::ScopedFD& fd) {
  int duped_fd = HANDLE_EINTR(dup(fd.get()));
  if (duped_fd < 0) {
    PLOG(ERROR) << "Failed to dup() client file descriptor";
    return base::ScopedFD();
  }
  return base::ScopedFD(duped_fd);
}

// Queries dbus-daemon for the Unix UID of |connection_name| via
// org.freedesktop.DBus.GetConnectionUnixUser.
std::optional<uid_t> GetConnectionUnixUser(dbus::Bus* bus,
                                           const std::string& connection_name) {
  dbus::ObjectProxy* dbus_proxy =
      bus->GetObjectProxy(DBUS_SERVICE_DBUS, dbus::ObjectPath(DBUS_PATH_DBUS));
  if (!dbus_proxy) {
    LOG(ERROR) << "Failed to get ObjectProxy for " << DBUS_SERVICE_DBUS;
    return std::nullopt;
  }
  dbus::MethodCall method_call(DBUS_INTERFACE_DBUS, "GetConnectionUnixUser");
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(connection_name);

  auto response = dbus_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response.has_value() || !response.value()) {
    LOG(ERROR) << "Failed to call GetConnectionUnixUser for "
               << connection_name;
    return std::nullopt;
  }
  dbus::MessageReader reader(response.value().get());
  uint32_t uid = 0;
  if (!reader.PopUint32(&uid)) {
    LOG(ERROR) << "Failed to read UID from GetConnectionUnixUser response";
    return std::nullopt;
  }
  return static_cast<uid_t>(uid);
}
}  // namespace

PatchpanelAdaptor::PatchpanelAdaptor(const base::FilePath& cmd_path,
                                     scoped_refptr<::dbus::Bus> bus,
                                     System* system,
                                     net_base::ProcessManager* process_manager,
                                     MetricsLibraryInterface* metrics,
                                     std::unique_ptr<RTNLClient> rtnl_client)
    : org::chromium::PatchPanelAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kPatchPanelServicePath)),
      bus_(bus),
      metrics_(metrics),
      manager_(std::make_unique<Manager>(cmd_path,
                                         bus,
                                         system,
                                         process_manager,
                                         metrics_,
                                         this,
                                         ShillClient::New(bus, system),
                                         std::move(rtnl_client))) {}

PatchpanelAdaptor::~PatchpanelAdaptor() = default;

void PatchpanelAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

bool CheckConciergeSender(dbus::Message* message,
                          dbus::Bus* bus,
                          brillo::ErrorPtr* error) {
  if (!message) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_INVALID_ARGS, "Missing message");
    return false;
  }
  const std::string sender = message->GetSender();
  if (sender.empty()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_INVALID_ARGS, "Empty sender");
    return false;
  }
  if (!bus) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Missing bus");
    return false;
  }
  const std::string owner = bus->GetServiceOwnerAndBlock(
      vm_tools::concierge::kVmConciergeServiceName, dbus::Bus::SUPPRESS_ERRORS);
  if (!owner.empty() && sender == owner) {
    return true;
  }
  // Also allow root (UID 0) callers (e.g. local integration tests such as
  // tast.vm.VhostUserNet that invoke VM startup/shutdown RPCs directly without
  // vm_concierge).
  const std::optional<uid_t> uid = GetConnectionUnixUser(bus, sender);
  if (uid.has_value() && *uid == 0) {
    return true;
  }
  LOG(ERROR) << "Expected sender to be "
             << vm_tools::concierge::kVmConciergeServiceName << " but got "
             << sender;
  brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                       DBUS_ERROR_ACCESS_DENIED, "Sender not authorized");
  return false;
}

bool PatchpanelAdaptor::ArcVmShutdown(brillo::ErrorPtr* error,
                                      dbus::Message* message,
                                      const ArcVmShutdownRequest& request,
                                      ArcVmShutdownResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  LOG(INFO) << "ARCVM shutting down";
  RecordDbusEvent(DbusUmaEvent::kArcVmShutdown);

  manager_->ArcVmShutdown(request.cid());
  RecordDbusEvent(DbusUmaEvent::kArcVmShutdownSuccess);
  return true;
}

bool PatchpanelAdaptor::ArcVmStartup(brillo::ErrorPtr* error,
                                     dbus::Message* message,
                                     const ArcVmStartupRequest& request,
                                     ArcVmStartupResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  LOG(INFO) << "ARCVM starting up";
  RecordDbusEvent(DbusUmaEvent::kArcVmStartup);

  const auto manager_response = manager_->ArcVmStartup(request.cid());
  if (!manager_response) {
    LOG(ERROR) << "Failed to start ARCVM network service";
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED,
                         "Failed to start ARCVM network service");
    return false;
  }
  *response = *manager_response;
  RecordDbusEvent(DbusUmaEvent::kArcVmStartupSuccess);
  return true;
}

ConnectNamespaceResponse PatchpanelAdaptor::ConnectNamespace(
    const ConnectNamespaceRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kConnectNamespace);

  const auto response = manager_->ConnectNamespace(request, Dup(client_fd));
  if (!response.netns_name().empty()) {
    RecordDbusEvent(DbusUmaEvent::kConnectNamespaceSuccess);
  }
  return response;
}

LocalOnlyNetworkResponse PatchpanelAdaptor::CreateLocalOnlyNetwork(
    const LocalOnlyNetworkRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kCreateLocalOnlyNetwork);
  LocalOnlyNetworkResponse response =
      manager_->downstream_network_service()->CreateLocalOnlyNetwork(
          request, Dup(client_fd));
  if (response.response_code() == DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(DbusUmaEvent::kCreateLocalOnlyNetworkSuccess);
  }
  metrics_->SendEnumToUMA(
      kCreateLocalOnlyNetworkUmaEventMetrics,
      DownstreamNetworkResultToUMAEvent(response.response_code()));
  return response;
}

TetheredNetworkResponse PatchpanelAdaptor::CreateTetheredNetwork(
    const TetheredNetworkRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kCreateTetheredNetwork);
  TetheredNetworkResponse response =
      manager_->downstream_network_service()->CreateTetheredNetwork(
          request, Dup(client_fd));
  if (response.response_code() == DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(DbusUmaEvent::kCreateTetheredNetworkSuccess);
  }
  metrics_->SendEnumToUMA(
      kCreateTetheredNetworkUmaEventMetrics,
      DownstreamNetworkResultToUMAEvent(response.response_code()));
  return response;
}

ConfigureNetworkResponse PatchpanelAdaptor::ConfigureNetwork(
    const ConfigureNetworkRequest& request) {
  RecordDbusEvent(DbusUmaEvent::kConfigureNetwork);

  const int ifindex = request.ifindex();
  const std::string& ifname = request.ifname();
  const net_base::NetworkConfig network_config =
      DeserializeNetworkConfig(request.network_config());

  net_base::NetworkPriority priority;
  priority.is_primary_logical = request.priority().is_primary_logical();
  priority.is_primary_physical = request.priority().is_primary_physical();
  priority.is_primary_for_dns = request.priority().is_primary_for_dns();
  priority.ranking_order = request.priority().ranking_order();

  const int session_id = request.session_id();

  // TODO(b/289971126): Unify the multiple enums used for technology.
  NetworkApplier::Technology technology;
  switch (request.technology()) {
    case patchpanel::NetworkTechnology::CELLULAR:
      technology = NetworkApplier::Technology::kCellular;
      break;
    case patchpanel::NetworkTechnology::ETHERNET:
      technology = NetworkApplier::Technology::kEthernet;
      break;
    case patchpanel::NetworkTechnology::VPN:
      technology = NetworkApplier::Technology::kVPN;
      break;
    case patchpanel::NetworkTechnology::WIFI:
      technology = NetworkApplier::Technology::kWiFi;
      break;
    default:
      technology = NetworkApplier::Technology::kEthernet;
  }

  manager_->ConfigureNetwork(ifindex, ifname,
                             static_cast<NetworkApplier::Area>(request.area()),
                             network_config, priority, technology, session_id);

  ConfigureNetworkResponse response;
  response.set_success(true);
  RecordDbusEvent(DbusUmaEvent::kConfigureNetworkSuccess);
  return response;
}

GetDevicesResponse PatchpanelAdaptor::GetDevices(
    const GetDevicesRequest& request) const {
  return manager_->GetDevices();
}

GetDownstreamNetworkInfoResponse PatchpanelAdaptor::GetDownstreamNetworkInfo(
    const GetDownstreamNetworkInfoRequest& request) const {
  RecordDbusEvent(DbusUmaEvent::kGetDownstreamNetworkInfo);
  GetDownstreamNetworkInfoResponse response =
      manager_->downstream_network_service()->GetDownstreamNetworkInfo(
          request.downstream_ifname());
  if (response.success()) {
    RecordDbusEvent(DbusUmaEvent::kGetDownstreamNetworkInfoSuccess);
  }
  return response;
}

TrafficCountersResponse PatchpanelAdaptor::GetTrafficCounters(
    const TrafficCountersRequest& request) const {
  RecordDbusEvent(DbusUmaEvent::kGetTrafficCounters);

  const std::set<std::string> shill_devices{request.devices().begin(),
                                            request.devices().end()};
  const auto counters = manager_->GetTrafficCounters(shill_devices);

  TrafficCountersResponse response;
  for (const auto& kv : counters) {
    auto* traffic_counter = response.add_counters();
    const auto& key = kv.first;
    const auto& counter = kv.second;
    traffic_counter->set_source(key.source);
    traffic_counter->set_device(key.ifname);
    traffic_counter->set_ip_family(key.ip_family);
    traffic_counter->set_rx_bytes(counter.rx_bytes);
    traffic_counter->set_rx_packets(counter.rx_packets);
    traffic_counter->set_tx_bytes(counter.tx_bytes);
    traffic_counter->set_tx_packets(counter.tx_packets);
  }

  RecordDbusEvent(DbusUmaEvent::kGetTrafficCountersSuccess);
  return response;
}

ModifyPortRuleResponse PatchpanelAdaptor::ModifyPortRule(
    const ModifyPortRuleRequest& request) {
  RecordDbusEvent(DbusUmaEvent::kModifyPortRule);

  const bool success = manager_->ModifyPortRule(request);
  if (success) {
    RecordDbusEvent(DbusUmaEvent::kModifyPortRuleSuccess);
  }

  ModifyPortRuleResponse response;
  response.set_success(success);
  return response;
}

bool PatchpanelAdaptor::BruschettaVmShutdown(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const BruschettaVmShutdownRequest& request,
    BruschettaVmShutdownResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  LOG(INFO) << "Bruschetta VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmShutdown);

  manager_->BruschettaVmShutdown(request.id());

  RecordDbusEvent(DbusUmaEvent::kBruschettaVmShutdownSuccess);
  return true;
}

bool PatchpanelAdaptor::BruschettaVmStartup(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const BruschettaVmStartupRequest& request,
    BruschettaVmStartupResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  const uint64_t vm_id = request.id();
  LOG(INFO) << __func__ << "(cid: " << vm_id << ")";
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmStartup);

  const auto* const bruschetta_device = manager_->BruschettaVmStartup(vm_id);
  if (!bruschetta_device) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << "): Failed to create virtual Device";
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Failed to create virtual Device");
    return false;
  }
  FillBruschettaAllocationProto(*bruschetta_device, response);
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmStartupSuccess);
  return true;
}

SetDnsRedirectionRuleResponse PatchpanelAdaptor::SetDnsRedirectionRule(
    const SetDnsRedirectionRuleRequest& request,
    const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kSetDnsRedirectionRule);

  const bool success = manager_->SetDnsRedirectionRule(request, Dup(client_fd));
  if (success) {
    RecordDbusEvent(DbusUmaEvent::kSetDnsRedirectionRuleSuccess);
  }

  SetDnsRedirectionRuleResponse response;
  response.set_success(success);
  return response;
}

SetVpnLockdownResponse PatchpanelAdaptor::SetVpnLockdown(
    const SetVpnLockdownRequest& request) {
  RecordDbusEvent(DbusUmaEvent::kSetVpnLockdown);

  manager_->SetVpnLockdown(request.enable_vpn_lockdown());

  RecordDbusEvent(DbusUmaEvent::kSetVpnLockdownSuccess);
  return {};
}

TagSocketResponse PatchpanelAdaptor::TagSocket(
    const TagSocketRequest& request, const base::ScopedFD& socket_fd) {
  RecordDbusEvent(DbusUmaEvent::kTagSocket);

  const bool success = manager_->TagSocket(request, socket_fd);
  if (success) {
    RecordDbusEvent(DbusUmaEvent::kTagSocketSuccess);
  }

  TagSocketResponse response;
  response.set_success(success);
  return response;
}

bool PatchpanelAdaptor::TerminaVmShutdown(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const TerminaVmShutdownRequest& request,
    TerminaVmShutdownResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  LOG(INFO) << "Termina VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kTerminaVmShutdown);

  manager_->TerminaVmShutdown(request.cid());

  RecordDbusEvent(DbusUmaEvent::kTerminaVmShutdownSuccess);
  return true;
}

bool PatchpanelAdaptor::TerminaVmStartup(brillo::ErrorPtr* error,
                                         dbus::Message* message,
                                         const TerminaVmStartupRequest& request,
                                         TerminaVmStartupResponse* response) {
  if (!CheckConciergeSender(message, bus_.get(), error)) {
    return false;
  }
  const uint32_t cid = request.cid();
  LOG(INFO) << __func__ << "(cid: " << cid << ")";
  RecordDbusEvent(DbusUmaEvent::kTerminaVmStartup);
  const auto* termina_device = manager_->TerminaVmStartup(cid);
  if (!termina_device) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Failed to create virtual Device";
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Failed to create virtual Device");
    return false;
  }
  if (!termina_device->lxd_ipv4_subnet()) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Missing LXD container IPv4 subnet";
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED,
                         "Missing LXD container IPv4 subnet");
    return false;
  }
  if (!termina_device->lxd_ipv4_address()) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Missing LXD container IPv4 address";
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED,
                         "Missing LXD container IPv4 address");
    return false;
  }
  FillTerminaAllocationProto(*termina_device, response);
  RecordDbusEvent(DbusUmaEvent::kTerminaVmStartupSuccess);
  return true;
}

NotifyAndroidWifiMulticastLockChangeResponse
PatchpanelAdaptor::NotifyAndroidWifiMulticastLockChange(
    const NotifyAndroidWifiMulticastLockChangeRequest& request) {
  manager_->NotifyAndroidWifiMulticastLockChange(request.held());
  return {};
}

NotifyAndroidInteractiveStateResponse
PatchpanelAdaptor::NotifyAndroidInteractiveState(
    const NotifyAndroidInteractiveStateRequest& request) {
  manager_->NotifyAndroidInteractiveState(request.interactive());
  return {};
}

NotifySocketConnectionEventResponse
PatchpanelAdaptor::NotifySocketConnectionEvent(
    const NotifySocketConnectionEventRequest& request) {
  manager_->NotifySocketConnectionEvent(request);
  return {};
}

NotifyARCVPNSocketConnectionEventResponse
PatchpanelAdaptor::NotifyARCVPNSocketConnectionEvent(
    const NotifyARCVPNSocketConnectionEventRequest& request) {
  manager_->NotifyARCVPNSocketConnectionEvent(request);
  return {};
}

SetFeatureFlagResponse PatchpanelAdaptor::SetFeatureFlag(
    const SetFeatureFlagRequest& request) {
  bool old_value = manager_->SetFeatureFlag(request.flag(), request.enabled());
  SetFeatureFlagResponse response;
  response.set_enabled(old_value);
  return response;
}

void PatchpanelAdaptor::OnNetworkDeviceChanged(
    std::unique_ptr<NetworkDevice> virtual_device,
    NetworkDeviceChangedSignal::Event event) {
  NetworkDeviceChangedSignal signal;
  signal.set_event(event);
  signal.set_allocated_device(virtual_device.release());  // Passes ownership
  SendNetworkDeviceChangedSignal(signal);
}

void PatchpanelAdaptor::OnNetworkConfigurationChanged() {
  NetworkConfigurationChangedSignal signal;
  SendNetworkConfigurationChangedSignal(signal);
}

void PatchpanelAdaptor::OnNeighborReachabilityEvent(
    int ifindex,
    const net_base::IPAddress& ip_addr,
    NeighborLinkMonitor::NeighborRole role,
    NeighborReachabilityEventSignal::EventType event_type) {
  NeighborReachabilityEventSignal signal;
  signal.set_ifindex(ifindex);
  signal.set_ip_addr(ip_addr.ToString());
  signal.set_type(event_type);
  switch (role) {
    case NeighborLinkMonitor::NeighborRole::kGateway:
      signal.set_role(NeighborReachabilityEventSignal::GATEWAY);
      break;
    case NeighborLinkMonitor::NeighborRole::kDNSServer:
      signal.set_role(NeighborReachabilityEventSignal::DNS_SERVER);
      break;
    case NeighborLinkMonitor::NeighborRole::kGatewayAndDNSServer:
      signal.set_role(NeighborReachabilityEventSignal::GATEWAY_AND_DNS_SERVER);
      break;
    default:
      NOTREACHED();
  }
  SendNeighborReachabilityEventSignal(signal);
}

void PatchpanelAdaptor::RecordDbusEvent(DbusUmaEvent event) const {
  metrics_->SendEnumToUMA(kDbusUmaEventMetrics, event);
}

}  // namespace patchpanel
