// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/patchpanel_adaptor.h"

#include <set>
#include <string>
#include <utility>

#include <chromeos/dbus/patchpanel/dbus-constants.h>
#include <metrics/metrics_library.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>
#include <shill/net/process_manager.h>

#include "patchpanel/downstream_network_service.h"
#include "patchpanel/manager.h"
#include "patchpanel/metrics.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/rtnl_client.h"

namespace patchpanel {

PatchpanelAdaptor::PatchpanelAdaptor(const base::FilePath& cmd_path,
                                     scoped_refptr<::dbus::Bus> bus,
                                     System* system,
                                     shill::ProcessManager* process_manager,
                                     MetricsLibraryInterface* metrics,
                                     std::unique_ptr<RTNLClient> rtnl_client)
    : org::chromium::PatchPanelAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kPatchPanelServicePath)),
      metrics_(metrics),
      manager_(
          std::make_unique<Manager>(cmd_path,
                                    system,
                                    process_manager,
                                    metrics_,
                                    this,
                                    std::make_unique<ShillClient>(bus, system),
                                    std::move(rtnl_client))) {}

PatchpanelAdaptor::~PatchpanelAdaptor() {}

void PatchpanelAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

ArcShutdownResponse PatchpanelAdaptor::ArcShutdown(
    const ArcShutdownRequest& request) {
  LOG(INFO) << "ARC++ shutting down";
  RecordDbusEvent(DbusUmaEvent::kArcShutdown);

  manager_->ArcShutdown();
  RecordDbusEvent(DbusUmaEvent::kArcShutdownSuccess);
  return {};
}

ArcStartupResponse PatchpanelAdaptor::ArcStartup(
    const ArcStartupRequest& request) {
  LOG(INFO) << "ARC++ starting up";
  RecordDbusEvent(DbusUmaEvent::kArcStartup);

  if (!manager_->ArcStartup(request.pid())) {
    LOG(ERROR) << "Failed to start ARC++ network service";
  } else {
    RecordDbusEvent(DbusUmaEvent::kArcStartupSuccess);
  }
  return {};
}

ArcVmShutdownResponse PatchpanelAdaptor::ArcVmShutdown(
    const ArcVmShutdownRequest& request) {
  LOG(INFO) << "ARCVM shutting down";
  RecordDbusEvent(DbusUmaEvent::kArcVmShutdown);

  manager_->ArcVmShutdown(request.cid());
  RecordDbusEvent(DbusUmaEvent::kArcVmShutdownSuccess);
  return {};
}

ArcVmStartupResponse PatchpanelAdaptor::ArcVmStartup(
    const ArcVmStartupRequest& request) {
  LOG(INFO) << "ARCVM starting up";
  RecordDbusEvent(DbusUmaEvent::kArcVmStartup);

  const auto response = manager_->ArcVmStartup(request.cid());
  if (!response) {
    LOG(ERROR) << "Failed to start ARCVM network service";
    return {};
  }
  RecordDbusEvent(DbusUmaEvent::kArcVmStartupSuccess);
  return *response;
}

ConnectNamespaceResponse PatchpanelAdaptor::ConnectNamespace(
    const ConnectNamespaceRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kConnectNamespace);

  const auto response = manager_->ConnectNamespace(request, client_fd);
  if (!response.netns_name().empty()) {
    RecordDbusEvent(DbusUmaEvent::kConnectNamespaceSuccess);
  }
  return response;
}

LocalOnlyNetworkResponse PatchpanelAdaptor::CreateLocalOnlyNetwork(
    const LocalOnlyNetworkRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kCreateLocalOnlyNetwork);

  const auto response_code =
      manager_->CreateLocalOnlyNetwork(request, client_fd);
  if (response_code == patchpanel::DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(DbusUmaEvent::kCreateLocalOnlyNetworkSuccess);
  }
  metrics_->SendEnumToUMA(kCreateLocalOnlyNetworkUmaEventMetrics,
                          DownstreamNetworkResultToUMAEvent(response_code));

  LocalOnlyNetworkResponse response;
  response.set_response_code(response_code);
  return response;
}

TetheredNetworkResponse PatchpanelAdaptor::CreateTetheredNetwork(
    const TetheredNetworkRequest& request, const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kCreateTetheredNetwork);

  const auto response_code =
      manager_->CreateTetheredNetwork(request, client_fd);
  if (response_code == patchpanel::DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(DbusUmaEvent::kCreateTetheredNetworkSuccess);
  }
  metrics_->SendEnumToUMA(kCreateTetheredNetworkUmaEventMetrics,
                          DownstreamNetworkResultToUMAEvent(response_code));

  TetheredNetworkResponse response;
  response.set_response_code(response_code);
  return response;
}

GetDevicesResponse PatchpanelAdaptor::GetDevices(
    const GetDevicesRequest& request) const {
  return manager_->GetDevices();
}

GetDownstreamNetworkInfoResponse PatchpanelAdaptor::GetDownstreamNetworkInfo(
    const GetDownstreamNetworkInfoRequest& request) const {
  RecordDbusEvent(DbusUmaEvent::kGetDownstreamNetworkInfo);

  const auto& downstream_ifname = request.downstream_ifname();
  const auto downstream_info =
      manager_->GetDownstreamNetworkInfo(downstream_ifname);
  if (!downstream_info) {
    LOG(ERROR) << __func__ << ": no DownstreamNetwork for interface "
               << downstream_ifname;
    return {};
  }

  RecordDbusEvent(DbusUmaEvent::kGetDownstreamNetworkInfoSuccess);
  GetDownstreamNetworkInfoResponse response;
  response.set_success(true);
  FillDownstreamNetworkProto(downstream_info->first,
                             response.mutable_downstream_network());
  for (const auto& info : downstream_info->second) {
    FillNetworkClientInfoProto(info, response.add_clients_info());
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

ParallelsVmShutdownResponse PatchpanelAdaptor::ParallelsVmShutdown(
    const ParallelsVmShutdownRequest& request) {
  LOG(INFO) << "Parallels VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kParallelsVmShutdown);

  manager_->ParallelsVmShutdown(request.id());

  RecordDbusEvent(DbusUmaEvent::kParallelsVmShutdownSuccess);
  return {};
}

ParallelsVmStartupResponse PatchpanelAdaptor::ParallelsVmStartup(
    const ParallelsVmStartupRequest& request) {
  const int subnet_index = request.subnet_index();
  const uint64_t vm_id = request.id();
  LOG(INFO) << __func__ << "(cid: " << vm_id
            << ", subnet_index: " << subnet_index << ")";
  RecordDbusEvent(DbusUmaEvent::kParallelsVmStartup);

  if (subnet_index < 0) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << ", subnet_index: " << subnet_index
               << "): Invalid subnet index";
    return {};
  }
  const auto* const parallels_device =
      manager_->ParallelsVmStartup(vm_id, static_cast<uint32_t>(subnet_index));
  if (!parallels_device) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << ", subnet_index: " << subnet_index
               << "): Failed to create virtual Device";
    return {};
  }
  ParallelsVmStartupResponse response;
  FillParallelsAllocationProto(*parallels_device, &response);
  RecordDbusEvent(DbusUmaEvent::kParallelsVmStartupSuccess);
  return response;
}

BruschettaVmShutdownResponse PatchpanelAdaptor::BruschettaVmShutdown(
    const BruschettaVmShutdownRequest& request) {
  LOG(INFO) << "Bruschetta VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmShutdown);

  manager_->BruschettaVmShutdown(request.id());

  RecordDbusEvent(DbusUmaEvent::kBruschettaVmShutdownSuccess);
  return {};
}

BruschettaVmStartupResponse PatchpanelAdaptor::BruschettaVmStartup(
    const BruschettaVmStartupRequest& request) {
  const uint64_t vm_id = request.id();
  LOG(INFO) << __func__ << "(cid: " << vm_id << ")";
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmStartup);

  const auto* const bruschetta_device = manager_->BruschettaVmStartup(vm_id);
  if (!bruschetta_device) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << "): Failed to create virtual Device";
    return {};
  }
  BruschettaVmStartupResponse response;
  FillBruschettaAllocationProto(*bruschetta_device, &response);
  RecordDbusEvent(DbusUmaEvent::kBruschettaVmStartupSuccess);
  return response;
}

BorealisVmShutdownResponse PatchpanelAdaptor::BorealisVmShutdown(
    const BorealisVmShutdownRequest& request) {
  LOG(INFO) << "Borealis VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kBorealisVmShutdown);

  manager_->BorealisVmShutdown(request.id());

  RecordDbusEvent(DbusUmaEvent::kBorealisVmShutdownSuccess);
  return {};
}

BorealisVmStartupResponse PatchpanelAdaptor::BorealisVmStartup(
    const BorealisVmStartupRequest& request) {
  const uint64_t vm_id = request.id();
  LOG(INFO) << __func__ << "(cid: " << vm_id << ")";
  RecordDbusEvent(DbusUmaEvent::kBorealisVmStartup);

  const auto* const borealis_device = manager_->BorealisVmStartup(vm_id);
  if (!borealis_device) {
    LOG(ERROR) << __func__ << "(cid: " << vm_id
               << "): Failed to create virtual Device";
    return {};
  }
  BorealisVmStartupResponse response;
  FillBorealisAllocationProto(*borealis_device, &response);
  RecordDbusEvent(DbusUmaEvent::kBorealisVmStartupSuccess);
  return response;
}

SetDnsRedirectionRuleResponse PatchpanelAdaptor::SetDnsRedirectionRule(
    const SetDnsRedirectionRuleRequest& request,
    const base::ScopedFD& client_fd) {
  RecordDbusEvent(DbusUmaEvent::kSetDnsRedirectionRule);

  const bool success = manager_->SetDnsRedirectionRule(request, client_fd);
  if (success) {
    RecordDbusEvent(DbusUmaEvent::kSetDnsRedirectionRuleSuccess);
  }

  SetDnsRedirectionRuleResponse response;
  response.set_success(success);
  return response;
}

SetVpnIntentResponse PatchpanelAdaptor::SetVpnIntent(
    const SetVpnIntentRequest& request, const base::ScopedFD& socket_fd) {
  RecordDbusEvent(DbusUmaEvent::kSetVpnIntent);

  const bool success = manager_->SetVpnIntent(request.policy(), socket_fd);
  if (!success) {
    LOG(ERROR) << "Failed to set VpnIntent: " << request.policy();
    return {};
  }

  RecordDbusEvent(DbusUmaEvent::kSetVpnIntentSuccess);
  SetVpnIntentResponse response;
  response.set_success(true);
  return response;
}

SetVpnLockdownResponse PatchpanelAdaptor::SetVpnLockdown(
    const SetVpnLockdownRequest& request) {
  RecordDbusEvent(DbusUmaEvent::kSetVpnLockdown);

  manager_->SetVpnLockdown(request.enable_vpn_lockdown());

  RecordDbusEvent(DbusUmaEvent::kSetVpnLockdownSuccess);
  return {};
}

TerminaVmShutdownResponse PatchpanelAdaptor::TerminaVmShutdown(
    const TerminaVmShutdownRequest& request) {
  LOG(INFO) << "Termina VM shutting down";
  RecordDbusEvent(DbusUmaEvent::kTerminaVmShutdown);

  manager_->TerminaVmShutdown(request.cid());

  RecordDbusEvent(DbusUmaEvent::kTerminaVmShutdownSuccess);
  return {};
}

TerminaVmStartupResponse PatchpanelAdaptor::TerminaVmStartup(
    const TerminaVmStartupRequest& request) {
  const uint32_t cid = request.cid();
  LOG(INFO) << __func__ << "(cid: " << cid << ")";
  RecordDbusEvent(DbusUmaEvent::kTerminaVmStartup);
  const auto* termina_device = manager_->TerminaVmStartup(cid);
  if (!termina_device) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Failed to create virtual Device";
    return {};
  }
  if (!termina_device->lxd_ipv4_subnet()) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Missing LXD container IPv4 subnet";
    return {};
  }
  if (!termina_device->lxd_ipv4_address()) {
    LOG(ERROR) << __func__ << "(cid: " << cid << ")"
               << ": Missing LXD container IPv4 address";
    return {};
  }
  TerminaVmStartupResponse response;
  FillTerminaAllocationProto(*termina_device, &response);
  RecordDbusEvent(DbusUmaEvent::kTerminaVmStartupSuccess);
  return response;
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
