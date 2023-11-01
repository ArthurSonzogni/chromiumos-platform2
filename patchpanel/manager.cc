// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/manager.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/crostini_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/downstream_network_service.h"
#include "patchpanel/metrics.h"
#include "patchpanel/multicast_metrics.h"
#include "patchpanel/net_util.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/qos_service.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
// Delay to restart IPv6 in a namespace to trigger SLAAC in the kernel.
constexpr int kIPv6RestartDelayMs = 300;
}  // namespace

Manager::Manager(const base::FilePath& cmd_path,
                 System* system,
                 shill::ProcessManager* process_manager,
                 MetricsLibraryInterface* metrics,
                 DbusClientNotifier* dbus_client_notifier,
                 std::unique_ptr<ShillClient> shill_client,
                 std::unique_ptr<RTNLClient> rtnl_client)
    : system_(system),
      metrics_(metrics),
      dbus_client_notifier_(dbus_client_notifier),
      shill_client_(std::move(shill_client)),
      rtnl_client_(std::move(rtnl_client)) {
  DCHECK(rtnl_client_);

  datapath_ = std::make_unique<Datapath>(system);
  adb_proxy_ = std::make_unique<patchpanel::SubprocessController>(
      system, process_manager, cmd_path, "--adb_proxy_fd");
  mcast_proxy_ = std::make_unique<patchpanel::SubprocessController>(
      system, process_manager, cmd_path, "--mcast_proxy_fd");
  nd_proxy_ = std::make_unique<patchpanel::SubprocessController>(
      system, process_manager, cmd_path, "--nd_proxy_fd");

  adb_proxy_->Start();
  mcast_proxy_->Start();
  nd_proxy_->Start();

  routing_svc_ = std::make_unique<RoutingService>();
  counters_svc_ = std::make_unique<CountersService>(datapath_.get());
  multicast_counters_svc_ =
      std::make_unique<MulticastCountersService>(datapath_.get());
  multicast_metrics_ = std::make_unique<MulticastMetrics>(
      multicast_counters_svc_.get(), metrics);

  datapath_->Start();
  multicast_counters_svc_->Start();
  multicast_metrics_->Start(MulticastMetrics::Type::kTotal);

  qos_svc_ = std::make_unique<QoSService>(datapath_.get());

  shill_client_->RegisterDevicesChangedHandler(base::BindRepeating(
      &Manager::OnShillDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterIPConfigsChangedHandler(base::BindRepeating(
      &Manager::OnIPConfigsChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterIPv6NetworkChangedHandler(base::BindRepeating(
      &Manager::OnIPv6NetworkChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDoHProvidersChangedHandler(base::BindRepeating(
      &Manager::OnDoHProvidersChanged, weak_factory_.GetWeakPtr()));

  auto arc_type =
      USE_ARCVM ? ArcService::ArcType::kVM : ArcService::ArcType::kContainer;
  arc_svc_ =
      std::make_unique<ArcService>(arc_type, datapath_.get(), &addr_mgr_, this,
                                   metrics_, dbus_client_notifier_);
  cros_svc_ = std::make_unique<CrostiniService>(&addr_mgr_, datapath_.get(),
                                                this, dbus_client_notifier_);

  network_monitor_svc_ = std::make_unique<NetworkMonitorService>(
      shill_client_.get(),
      base::BindRepeating(&Manager::OnNeighborReachabilityEvent,
                          weak_factory_.GetWeakPtr()));
  ipv6_svc_ = std::make_unique<GuestIPv6Service>(nd_proxy_.get(),
                                                 datapath_.get(), system);
  clat_svc_ =
      std::make_unique<ClatService>(datapath_.get(), process_manager, system);
  network_monitor_svc_->Start();
  ipv6_svc_->Start();

  // Shill client's default devices methods trigger the Manager's callbacks on
  // registration. Call them after everything is set up.
  shill_client_->RegisterDefaultLogicalDeviceChangedHandler(
      base::BindRepeating(&Manager::OnShillDefaultLogicalDeviceChanged,
                          weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDefaultPhysicalDeviceChangedHandler(
      base::BindRepeating(&Manager::OnShillDefaultPhysicalDeviceChanged,
                          weak_factory_.GetWeakPtr()));
}

Manager::~Manager() {
  network_monitor_svc_.reset();
  cros_svc_.reset();
  arc_svc_.reset();
  clat_svc_.reset();

  // Explicitly reset QoSService before Datapath::Stop() since the former one
  // depends on Datapath.
  qos_svc_.reset();

  // Tear down any remaining active lifeline file descriptors.
  std::vector<int> lifeline_fds;
  for (const auto& kv : connected_namespaces_) {
    lifeline_fds.push_back(kv.first);
  }
  for (const auto& kv : dns_redirection_rules_) {
    lifeline_fds.push_back(kv.first);
  }
  for (const int fdkey : lifeline_fds) {
    OnLifelineFdClosed(fdkey);
  }

  multicast_counters_svc_->Stop();
  datapath_->Stop();
}

void Manager::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  // Only take into account interface switches and new Device or removed Device
  // events. Ignore any layer 3 property change.
  if (!prev_device && !new_device) {
    return;
  }
  if (prev_device && new_device && prev_device->ifname == new_device->ifname) {
    return;
  }

  if (prev_device && prev_device->type == ShillClient::Device::Type::kVPN) {
    datapath_->StopVpnRouting(*prev_device);
    counters_svc_->OnVpnDeviceRemoved(prev_device->ifname);
  }

  if (new_device && new_device->type == ShillClient::Device::Type::kVPN) {
    counters_svc_->OnVpnDeviceAdded(new_device->ifname);
    datapath_->StartVpnRouting(*new_device);
  }

  cros_svc_->OnShillDefaultLogicalDeviceChanged(new_device, prev_device);

  // When the default logical network changes, ConnectedNamespaces' devices
  // which follow the logical network must leave their current forwarding group
  // for IPv6 ndproxy and join the forwarding group of the new logical default
  // network. This is marked by empty |outbound_ifname| and |route_on_vpn|
  // with the value of true.
  for (auto& [_, nsinfo] : connected_namespaces_) {
    if (!nsinfo.outbound_ifname.empty() || !nsinfo.route_on_vpn) {
      continue;
    }
    if (prev_device) {
      nsinfo.current_outbound_device = std::nullopt;
    }
    if (new_device) {
      nsinfo.current_outbound_device = *new_device;
    }

    // When IPv6 is configured statically, no need to update forwarding set and
    // restart IPv6 inside the namespace.
    if (nsinfo.static_ipv6_config.has_value()) {
      continue;
    }
    if (prev_device) {
      StopForwarding(*prev_device, nsinfo.host_ifname,
                     ForwardingService::ForwardingSet{.ipv6 = true});
    }
    if (new_device) {
      StartForwarding(*new_device, nsinfo.host_ifname,
                      ForwardingService::ForwardingSet{.ipv6 = true});

      // Disable and re-enable IPv6. This is necessary to trigger SLAAC in the
      // kernel to send RS. Add a delay for the forwarding to be set up.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                         nsinfo.netns_name),
          base::Milliseconds(kIPv6RestartDelayMs));
    }
  }
  clat_svc_->OnShillDefaultLogicalDeviceChanged(new_device, prev_device);
}

void Manager::OnShillDefaultPhysicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  // Only take into account interface switches and new Device or removed Device
  // events. Ignore any layer 3 property change.
  if (!prev_device && !new_device) {
    return;
  }
  if (prev_device && new_device && prev_device->ifname == new_device->ifname) {
    return;
  }

  // When the default physical network changes, ConnectedNamespaces' devices
  // which follow the physical network must leave their current forwarding group
  // for IPv6 ndproxy and join the forwarding group of the new physical default
  // network. This is marked by empty |outbound_ifname| and |route_on_vpn|
  // with the value of false.
  for (auto& [_, nsinfo] : connected_namespaces_) {
    if (!nsinfo.outbound_ifname.empty() || nsinfo.route_on_vpn) {
      continue;
    }
    if (prev_device) {
      nsinfo.current_outbound_device = std::nullopt;
    }
    if (new_device) {
      nsinfo.current_outbound_device = *new_device;
    }

    // When IPv6 is configured statically, no need to update forwarding set and
    // restart IPv6 inside the namespace.
    if (nsinfo.static_ipv6_config.has_value()) {
      continue;
    }
    if (prev_device) {
      StopForwarding(*prev_device, nsinfo.host_ifname,
                     ForwardingService::ForwardingSet{.ipv6 = true});
    }
    if (new_device) {
      StartForwarding(*new_device, nsinfo.host_ifname,
                      ForwardingService::ForwardingSet{.ipv6 = true});

      // Disable and re-enable IPv6. This is necessary to trigger SLAAC in the
      // kernel to send RS. Add a delay for the forwarding to be set up.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                         nsinfo.netns_name),
          base::Milliseconds(kIPv6RestartDelayMs));
    }
  }
}

void Manager::RestartIPv6(const std::string& netns_name) {
  auto ns = ScopedNS::EnterNetworkNS(netns_name);
  if (!ns) {
    LOG(ERROR) << "Invalid namespace name " << netns_name;
    return;
  }

  if (datapath_) {
    datapath_->RestartIPv6();
  }
}

void Manager::OnShillDevicesChanged(
    const std::vector<ShillClient::Device>& added,
    const std::vector<ShillClient::Device>& removed) {
  // Rules for traffic counters should be installed at the first and removed at
  // the last to make sure every packet is counted.
  for (const auto& device : removed) {
    for (auto& [_, nsinfo] : connected_namespaces_) {
      if (nsinfo.outbound_ifname != device.ifname) {
        continue;
      }
      if (nsinfo.static_ipv6_config.has_value()) {
        continue;
      }
      StopForwarding(device, nsinfo.host_ifname,
                     ForwardingService::ForwardingSet{.ipv6 = true});
    }
    StopForwarding(device, /*ifname_virtual=*/"");
    datapath_->StopConnectionPinning(device);
    datapath_->RemoveRedirectDnsRule(device);
    arc_svc_->RemoveDevice(device);
    multicast_metrics_->OnPhysicalDeviceRemoved(device);
    counters_svc_->OnPhysicalDeviceRemoved(device.ifname);
    multicast_counters_svc_->OnPhysicalDeviceRemoved(device);
    qos_svc_->OnPhysicalDeviceRemoved(device);

    // We have no good way to tell whether the removed Device was cellular now,
    // so we always call this. StopSourcePrefixEnforcement will find out by
    // matching |ifname| with existing rules.
    // TODO(hugobenichi): fix the above problem now that the full Device
    // information is  available.
    datapath_->StopSourceIPv6PrefixEnforcement(device);
  }

  for (const auto& device : added) {
    qos_svc_->OnPhysicalDeviceAdded(device);
    counters_svc_->OnPhysicalDeviceAdded(device.ifname);
    multicast_counters_svc_->OnPhysicalDeviceAdded(device);
    multicast_metrics_->OnPhysicalDeviceAdded(device);
    for (auto& [_, nsinfo] : connected_namespaces_) {
      if (nsinfo.outbound_ifname != device.ifname) {
        continue;
      }
      if (nsinfo.static_ipv6_config.has_value()) {
        continue;
      }
      StartForwarding(device, nsinfo.host_ifname,
                      ForwardingService::ForwardingSet{.ipv6 = true});
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                         nsinfo.netns_name),
          base::Milliseconds(kIPv6RestartDelayMs));
    }
    datapath_->StartConnectionPinning(device);

    if (!device.ipconfig.ipv4_dns_addresses.empty()) {
      datapath_->AddRedirectDnsRule(device,
                                    device.ipconfig.ipv4_dns_addresses.front());
    }

    arc_svc_->AddDevice(device);
    if (device.type == ShillClient::Device::Type::kCellular) {
      datapath_->StartSourceIPv6PrefixEnforcement(device);
    }
  }
}

void Manager::OnIPConfigsChanged(const ShillClient::Device& shill_device) {
  if (shill_device.ipconfig.ipv4_dns_addresses.empty()) {
    datapath_->RemoveRedirectDnsRule(shill_device);
  } else {
    datapath_->AddRedirectDnsRule(
        shill_device, shill_device.ipconfig.ipv4_dns_addresses.front());
  }
  multicast_metrics_->OnIPConfigsChanged(shill_device);
  ipv6_svc_->UpdateUplinkIPv6DNS(shill_device);

  // Update local copies of the ShillClient::Device to keep IP configuration
  // properties in sync.
  for (auto& [_, info] : downstream_networks_) {
    if (info.upstream_device &&
        info.upstream_device->ifname == shill_device.ifname) {
      info.upstream_device = shill_device;
    }
  }
  for (auto& [_, nsinfo] : connected_namespaces_) {
    if (!nsinfo.current_outbound_device) {
      continue;
    }
    if (nsinfo.current_outbound_device->ifname == shill_device.ifname) {
      nsinfo.current_outbound_device = shill_device;
    }
  }

  arc_svc_->UpdateDeviceIPConfig(shill_device);

  const auto* default_logical_device = shill_client_->default_logical_device();
  if (default_logical_device &&
      shill_device.ifname == default_logical_device->ifname) {
    clat_svc_->OnDefaultLogicalDeviceIPConfigChanged(shill_device);
  }
}

void Manager::OnIPv6NetworkChanged(const ShillClient::Device& shill_device) {
  ipv6_svc_->OnUplinkIPv6Changed(shill_device);

  if (!shill_device.ipconfig.ipv6_cidr) {
    if (shill_device.type == ShillClient::Device::Type::kCellular) {
      datapath_->UpdateSourceEnforcementIPv6Prefix(shill_device, std::nullopt);
    }
    return;
  }

  for (auto& [_, nsinfo] : connected_namespaces_) {
    if (nsinfo.outbound_ifname != shill_device.ifname) {
      continue;
    }

    if (nsinfo.static_ipv6_config.has_value()) {
      continue;
    }
    // Disable and re-enable IPv6 inside the namespace. This is necessary to
    // trigger SLAAC in the kernel to send RS.
    RestartIPv6(nsinfo.netns_name);
  }

  if (shill_device.type == ShillClient::Device::Type::kCellular) {
    // TODO(b/279871350): Support prefix shorter than /64.
    const auto prefix = GuestIPv6Service::IPAddressTo64BitPrefix(
        shill_device.ipconfig.ipv6_cidr->address());
    datapath_->UpdateSourceEnforcementIPv6Prefix(shill_device, prefix);
  }
}

void Manager::OnDoHProvidersChanged(
    const ShillClient::DoHProviders& doh_providers) {
  qos_svc_->UpdateDoHProviders(doh_providers);
}

bool Manager::ArcStartup(pid_t pid) {
  if (pid < 0) {
    LOG(ERROR) << "Invalid ARC pid: " << pid;
    return false;
  }

  if (!arc_svc_->Start(static_cast<uint32_t>(pid)))
    return false;

  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC);
  msg.set_arc_pid(pid);
  SendGuestMessage(msg);

  multicast_metrics_->OnARCStarted();

  return true;
}

void Manager::ArcShutdown() {
  multicast_metrics_->OnARCStopped();

  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC);
  SendGuestMessage(msg);

  // After the ARC container has stopped, the pid is not known anymore.
  // The pid argument is ignored by ArcService.
  arc_svc_->Stop(0);
}

std::optional<patchpanel::ArcVmStartupResponse> Manager::ArcVmStartup(
    uint32_t cid) {
  if (!arc_svc_->Start(cid)) {
    return std::nullopt;
  }
  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC_VM);
  msg.set_arcvm_vsock_cid(cid);
  SendGuestMessage(msg);

  multicast_metrics_->OnARCStarted();

  patchpanel::ArcVmStartupResponse response;
  if (const auto arc0_addr = arc_svc_->GetArc0IPv4Address()) {
    response.set_arc0_ipv4_address(arc0_addr->ToByteString());
  }
  for (const auto& tap : arc_svc_->GetTapDevices()) {
    response.add_tap_device_ifnames(tap);
  }
  return response;
}

void Manager::ArcVmShutdown(uint32_t cid) {
  multicast_metrics_->OnARCStopped();

  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC_VM);
  msg.set_arcvm_vsock_cid(cid);
  SendGuestMessage(msg);

  arc_svc_->Stop(cid);
}

const CrostiniService::CrostiniDevice* Manager::StartCrosVm(
    uint64_t vm_id, CrostiniService::VMType vm_type, uint32_t subnet_index) {
  const auto* guest_device = cros_svc_->Start(vm_id, vm_type, subnet_index);
  if (!guest_device) {
    return nullptr;
  }
  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(CrostiniService::GuestMessageTypeFromVMType(vm_type));
  SendGuestMessage(msg);
  return guest_device;
}

void Manager::StopCrosVm(uint64_t vm_id, CrostiniService::VMType vm_type) {
  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(CrostiniService::GuestMessageTypeFromVMType(vm_type));
  SendGuestMessage(msg);
  cros_svc_->Stop(vm_id);
}

GetDevicesResponse Manager::GetDevices() const {
  GetDevicesResponse response;

  for (const auto* arc_device : arc_svc_->GetDevices()) {
    // The legacy "arc0" Device is never exposed in "GetDevices".
    if (!arc_device->shill_device_ifname()) {
      continue;
    }
    auto* dev = response.add_devices();
    arc_device->ConvertToProto(dev);
    FillArcDeviceDnsProxyProto(*arc_device, dev, dns_proxy_ipv4_addrs_,
                               dns_proxy_ipv6_addrs_);
  }

  for (const auto* crostini_device : cros_svc_->GetDevices()) {
    crostini_device->ConvertToProto(response.add_devices());
  }

  return response;
}

const CrostiniService::CrostiniDevice* const Manager::TerminaVmStartup(
    uint64_t cid) {
  const auto* guest_device =
      StartCrosVm(cid, CrostiniService::VMType::kTermina);
  if (!guest_device) {
    LOG(ERROR) << "Failed to start Termina VM network service";
    return nullptr;
  }
  return guest_device;
}

void Manager::TerminaVmShutdown(uint64_t vm_id) {
  StopCrosVm(vm_id, CrostiniService::VMType::kTermina);
}

const CrostiniService::CrostiniDevice* const Manager::ParallelsVmStartup(
    uint64_t vm_id, uint32_t subnet_index) {
  const auto* guest_device =
      StartCrosVm(vm_id, CrostiniService::VMType::kParallels, subnet_index);
  if (!guest_device) {
    LOG(ERROR) << "Failed to start Parallels VM network service";
    return nullptr;
  }
  return guest_device;
}

void Manager::ParallelsVmShutdown(uint64_t vm_id) {
  StopCrosVm(vm_id, CrostiniService::VMType::kParallels);
}

const CrostiniService::CrostiniDevice* const Manager::BruschettaVmStartup(
    uint64_t vm_id) {
  const auto* guest_device =
      StartCrosVm(vm_id, CrostiniService::VMType::kBruschetta);
  if (!guest_device) {
    LOG(ERROR) << "Failed to start Bruschetta VM network service";
    return nullptr;
  }
  return guest_device;
}

void Manager::BruschettaVmShutdown(uint64_t vm_id) {
  StopCrosVm(vm_id, CrostiniService::VMType::kBruschetta);
}

bool Manager::SetVpnIntent(SetVpnIntentRequest::VpnRoutingPolicy policy,
                           const base::ScopedFD& sockfd) {
  return routing_svc_->SetVpnFwmark(sockfd.get(), policy);
}

std::map<CountersService::CounterKey, CountersService::Counter>
Manager::GetTrafficCounters(const std::set<std::string>& shill_devices) const {
  return counters_svc_->GetCounters(shill_devices);
}

bool Manager::ModifyPortRule(const ModifyPortRuleRequest& request) {
  return datapath_->ModifyPortRule(request);
}

void Manager::SetVpnLockdown(bool enable_vpn_lockdown) {
  datapath_->SetVpnLockdown(enable_vpn_lockdown);
}

patchpanel::DownstreamNetworkResult Manager::CreateTetheredNetwork(
    const TetheredNetworkRequest& request, const base::ScopedFD& client_fd) {
  // b/273741099, b/293964582: patchpanel must support callers using either the
  // shill Device kInterfaceProperty value (Cellular multiplexing disabled) or
  // the kPrimaryMultiplexedInterfaceProperty value (Cellular multiplexing
  // enabled). This can be achieved by comparing the interface name specified by
  // the request for the upstream network with the |ifname| value of the
  // ShillClient's Devices.
  std::optional<ShillClient::Device> upstream_shill_device = std::nullopt;
  for (const auto& shill_device : shill_client_->GetDevices()) {
    if (shill_device.ifname == request.upstream_ifname()) {
      upstream_shill_device = shill_device;
      break;
    }
  }
  if (!upstream_shill_device) {
    // b/294287313: if the tethering request is asking for a multiplexed PDN
    // request, ShillClient has no knowledge of the associated Network as there
    // are no shill Device associated with the Network. If the network interface
    // specified in the request exists, create a fake ShillClient::Device to
    // represent that tethering Network.
    upstream_shill_device = StartTetheringUpstreamNetwork(request);
    if (!upstream_shill_device) {
      LOG(ERROR) << "Unknown shill Device " << request.upstream_ifname();
      return patchpanel::DownstreamNetworkResult::UPSTREAM_UNKNOWN;
    }
  }
  const auto info =
      DownstreamNetworkInfo::Create(request, *upstream_shill_device);
  if (!info) {
    LOG(ERROR) << __func__ << ": Unable to parse request";
    return patchpanel::DownstreamNetworkResult::INVALID_REQUEST;
  }

  return HandleDownstreamNetworkInfo(client_fd, *info);
}

patchpanel::DownstreamNetworkResult Manager::CreateLocalOnlyNetwork(
    const LocalOnlyNetworkRequest& request, const base::ScopedFD& client_fd) {
  std::optional<DownstreamNetworkInfo> info =
      DownstreamNetworkInfo::Create(request);
  if (!info) {
    LOG(ERROR) << __func__ << ": Unable to parse request";
    return patchpanel::DownstreamNetworkResult::INVALID_REQUEST;
  }

  return HandleDownstreamNetworkInfo(client_fd, *info);
}

std::optional<
    std::pair<DownstreamNetworkInfo, std::vector<DownstreamClientInfo>>>
Manager::GetDownstreamNetworkInfo(const std::string& downstream_ifname) const {
  auto match_by_downstream_ifname = [&downstream_ifname](const auto& kv) {
    return kv.second.downstream_ifname == downstream_ifname;
  };

  const auto it =
      std::find_if(downstream_networks_.begin(), downstream_networks_.end(),
                   match_by_downstream_ifname);
  if (it == downstream_networks_.end()) {
    return std::nullopt;
  }

  return std::make_pair(it->second, GetDownstreamClientInfo(downstream_ifname));
}

std::vector<DownstreamClientInfo> Manager::GetDownstreamClientInfo(
    const std::string& downstream_ifname) const {
  const auto ifindex = system_->IfNametoindex(downstream_ifname);
  if (!ifindex) {
    LOG(WARNING) << "Failed to get index of the interface:" << downstream_ifname
                 << ", skip querying the client info";
    return {};
  }

  std::map<MacAddress,
           std::pair<net_base::IPv4Address, std::vector<net_base::IPv6Address>>>
      mac_to_ip;
  for (const auto& [ipv4_addr, mac_addr] :
       rtnl_client_->GetIPv4NeighborMacTable(ifindex)) {
    mac_to_ip[mac_addr].first = ipv4_addr;
  }
  for (const auto& [ipv6_addr, mac_addr] :
       rtnl_client_->GetIPv6NeighborMacTable(ifindex)) {
    mac_to_ip[mac_addr].second.push_back(ipv6_addr);
  }

  const auto dhcp_server_controller_iter =
      dhcp_server_controllers_.find(downstream_ifname);
  std::vector<DownstreamClientInfo> client_infos;
  for (const auto& [mac_addr, ip] : mac_to_ip) {
    std::string hostname = "";
    if (dhcp_server_controller_iter != dhcp_server_controllers_.end()) {
      hostname = dhcp_server_controller_iter->second->GetClientHostname(
          MacAddressToString(mac_addr));
    }

    client_infos.push_back(
        {mac_addr, ip.first, ip.second, hostname, /*vendor_class=*/""});
  }
  return client_infos;
}

std::optional<ShillClient::Device> Manager::StartTetheringUpstreamNetwork(
    const TetheredNetworkRequest& request) {
  const auto& upstream_ifname = request.upstream_ifname();
  const int ifindex = system_->IfNametoindex(upstream_ifname);
  if (ifindex < 0) {
    LOG(ERROR) << __func__ << ": unknown interface " << upstream_ifname;
    return std::nullopt;
  }

  // Assume the Network is a Cellular network, and assume there is a known
  // Cellular Device for the primary multiplexed Network already tracked by
  // ShillClient.
  ShillClient::Device upstream_network;
  for (const auto& shill_device : shill_client_->GetDevices()) {
    if (shill_device.type == ShillClient::Device::Type::kCellular) {
      // Copy the shill Device and Service properties common to both the primary
      // multiplexed Network and the tethering Network.
      upstream_network.shill_device_interface_property =
          shill_device.shill_device_interface_property;
      upstream_network.service_path = shill_device.service_path;
      break;
    }
  }
  if (upstream_network.shill_device_interface_property.empty()) {
    LOG(ERROR) << __func__
               << ": no Cellular ShillDevice to associate with tethering "
                  "uplink interface "
               << upstream_ifname;
    return std::nullopt;
  }
  upstream_network.type = ShillClient::Device::Type::kCellular;
  upstream_network.ifindex = ifindex;
  upstream_network.ifname = upstream_ifname;

  // Setup the datapath for this interface, as if the device was advertised in
  // OnShillDevicesChanged. We skip services or setup that don'tr apply to
  // cellular (multicast traffic counters) or that are not interacting with the
  // separate PDN network exclusively used for tethering (ConnectNamespace,
  // dns-proxy redirection, ArcService, CrostiniService, neighbor monitoring).
  LOG(INFO) << __func__ << ": Configuring datapath for fake shill Device "
            << upstream_network;
  counters_svc_->OnPhysicalDeviceAdded(upstream_ifname);
  datapath_->StartConnectionPinning(upstream_network);

  // b/294287313: copy the IPv6 configuration of the upstream Network
  // directly from shill's tethering request, notify GuestIPv6Service about
  // the prefix of the upstream Network, and also call
  // Datapath::StartSourceIPv6PrefixEnforcement()
  if (request.has_uplink_ipv6_config()) {
    upstream_network.ipconfig.ipv6_cidr =
        net_base::IPv6CIDR::CreateFromBytesAndPrefix(
            request.uplink_ipv6_config().uplink_ipv6_cidr().addr(),
            request.uplink_ipv6_config().uplink_ipv6_cidr().prefix_len());
    for (const auto& dns : request.uplink_ipv6_config().dns_servers()) {
      auto addr = net_base::IPv6Address::CreateFromBytes(dns);
      if (addr) {
        upstream_network.ipconfig.ipv6_dns_addresses.push_back(
            addr->ToString());
      }
    }
    if (upstream_network.ipconfig.ipv6_cidr) {
      ipv6_svc_->OnUplinkIPv6Changed(upstream_network);
      ipv6_svc_->UpdateUplinkIPv6DNS(upstream_network);
      datapath_->StartSourceIPv6PrefixEnforcement(upstream_network);
      // TODO(b/279871350): Support prefix shorter than /64.
      const auto ipv6_prefix = GuestIPv6Service::IPAddressTo64BitPrefix(
          upstream_network.ipconfig.ipv6_cidr->address());
      datapath_->UpdateSourceEnforcementIPv6Prefix(upstream_network,
                                                   ipv6_prefix);
    } else {
      LOG(WARNING) << __func__ << ": failed to parse uplink IPv6 configuration";
    }
  }

  return upstream_network;
}

void Manager::StopTetheringUpstreamNetwork(
    const ShillClient::Device& upstream_network) {
  LOG(INFO) << __func__ << ": Tearing down datapath for fake shill Device "
            << upstream_network;
  ipv6_svc_->StopUplink(upstream_network);
  datapath_->StopSourceIPv6PrefixEnforcement(upstream_network);
  datapath_->StopConnectionPinning(upstream_network);
  counters_svc_->OnPhysicalDeviceRemoved(upstream_network.ifname);
  // b/305257482: Ensure that GuestIPv6Service forgets the IPv6 configuration of
  // the upstream network by faking IPv6 disconnection.
  auto fake_disconneted_network = upstream_network;
  fake_disconneted_network.ipconfig.ipv6_cidr = std::nullopt;
  ipv6_svc_->OnUplinkIPv6Changed(fake_disconneted_network);
}

void Manager::OnNeighborReachabilityEvent(
    int ifindex,
    const net_base::IPAddress& ip_addr,
    NeighborLinkMonitor::NeighborRole role,
    NeighborReachabilityEventSignal::EventType event_type) {
  dbus_client_notifier_->OnNeighborReachabilityEvent(ifindex, ip_addr, role,
                                                     event_type);
}

ConnectNamespaceResponse Manager::ConnectNamespace(
    const ConnectNamespaceRequest& request, const base::ScopedFD& client_fd) {
  ConnectNamespaceResponse response;

  const pid_t pid = request.pid();
  if (pid == 1 || pid == getpid()) {
    LOG(ERROR) << "Privileged namespace pid " << pid;
    return response;
  }
  if (pid != ConnectedNamespace::kNewNetnsPid) {
    auto ns = ScopedNS::EnterNetworkNS(pid);
    if (!ns) {
      LOG(ERROR) << "Invalid namespace pid " << pid;
      return response;
    }
  }

  // Get the ConnectedNamespace outbound shill Device.
  // TODO(b/273744897): Migrate ConnectNamespace to use a patchpanel Network id
  // instead of the interface name of the shill Device.
  const std::string& outbound_ifname = request.outbound_physical_device();
  const ShillClient::Device* current_outbound_device;
  if (!outbound_ifname.empty()) {
    // b/273741099: For multiplexed Cellular interfaces, callers expect
    // patchpanel to accept a shill Device kInterfaceProperty value and swap it
    // with the name of the primary multiplexed interface.
    auto* shill_device =
        shill_client_->GetDeviceByShillDeviceName(outbound_ifname);
    if (!shill_device) {
      LOG(ERROR) << __func__ << ": no shill Device for upstream ifname "
                 << outbound_ifname;
      return response;
    }
    current_outbound_device = shill_device;
  } else if (request.route_on_vpn()) {
    current_outbound_device = shill_client_->default_logical_device();
  } else {
    current_outbound_device = shill_client_->default_physical_device();
  }

  std::unique_ptr<Subnet> ipv4_subnet =
      addr_mgr_.AllocateIPv4Subnet(AddressManager::GuestType::kNetns);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Exhausted IPv4 subnet space";
    return response;
  }

  const auto host_ipv4_cidr = ipv4_subnet->CIDRAtOffset(1);
  const auto peer_ipv4_cidr = ipv4_subnet->CIDRAtOffset(2);
  if (!host_ipv4_cidr || !peer_ipv4_cidr) {
    LOG(ERROR) << "Failed to create CIDR from subnet: "
               << ipv4_subnet->base_cidr();
    return response;
  }

  base::ScopedFD local_client_fd = AddLifelineFd(client_fd);
  if (!local_client_fd.is_valid()) {
    LOG(ERROR) << "Failed to create lifeline fd";
    return response;
  }

  const std::string ifname_id = std::to_string(connected_namespaces_next_id_);
  ConnectedNamespace nsinfo = {};
  nsinfo.pid = request.pid();
  nsinfo.netns_name = "connected_netns_" + ifname_id;
  nsinfo.source = ProtoToTrafficSource(request.traffic_source());
  if (nsinfo.source == TrafficSource::kUnknown)
    nsinfo.source = TrafficSource::kSystem;
  nsinfo.outbound_ifname = outbound_ifname;
  nsinfo.route_on_vpn = request.route_on_vpn();
  nsinfo.host_ifname = "arc_ns" + ifname_id;
  nsinfo.peer_ifname = "veth" + ifname_id;
  nsinfo.peer_ipv4_subnet = std::move(ipv4_subnet);
  nsinfo.host_ipv4_cidr = *host_ipv4_cidr;
  nsinfo.peer_ipv4_cidr = *peer_ipv4_cidr;
  nsinfo.host_mac_addr = addr_mgr_.GenerateMacAddress();
  nsinfo.peer_mac_addr = addr_mgr_.GenerateMacAddress();
  if (nsinfo.host_mac_addr == nsinfo.peer_mac_addr) {
    LOG(ERROR) << "Failed to generate unique MAC address for connected "
                  "namespace host and peer interface";
  }
  if (current_outbound_device) {
    nsinfo.current_outbound_device = *current_outbound_device;
  }
  if (request.static_ipv6()) {
    auto ipv6_subnet = addr_mgr_.AllocateIPv6Subnet();
    if (ipv6_subnet.prefix_length() >= 127) {
      LOG(ERROR) << "Allocated IPv6 subnet must at least hold 2 addresses and "
                    "1 base address, but got "
                 << ipv6_subnet;
    } else {
      nsinfo.static_ipv6_config = StaticIPv6Config{};
      nsinfo.static_ipv6_config->host_cidr =
          *addr_mgr_.GetRandomizedIPv6Address(ipv6_subnet);
      do {
        nsinfo.static_ipv6_config->peer_cidr =
            *addr_mgr_.GetRandomizedIPv6Address(ipv6_subnet);
      } while (nsinfo.static_ipv6_config->peer_cidr ==
               nsinfo.static_ipv6_config->host_cidr);
    }
  }

  if (!datapath_->StartRoutingNamespace(nsinfo)) {
    LOG(ERROR) << "Failed to setup datapath";
    if (!DeleteLifelineFd(local_client_fd.release())) {
      LOG(ERROR) << "Failed to delete lifeline fd";
    }
    return response;
  }

  response.set_peer_ifname(nsinfo.peer_ifname);
  response.set_peer_ipv4_address(peer_ipv4_cidr->address().ToInAddr().s_addr);
  response.set_host_ifname(nsinfo.host_ifname);
  response.set_host_ipv4_address(host_ipv4_cidr->address().ToInAddr().s_addr);
  response.set_netns_name(nsinfo.netns_name);
  auto* response_subnet = response.mutable_ipv4_subnet();
  FillSubnetProto(*nsinfo.peer_ipv4_subnet, response_subnet);

  LOG(INFO) << "Connected network namespace " << nsinfo;

  // Start forwarding for IPv6.
  if (!nsinfo.static_ipv6_config.has_value() && current_outbound_device) {
    StartForwarding(*current_outbound_device, nsinfo.host_ifname,
                    ForwardingService::ForwardingSet{.ipv6 = true});
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                       nsinfo.netns_name),
        base::Milliseconds(kIPv6RestartDelayMs));
  }

  // Store ConnectedNamespace
  connected_namespaces_next_id_++;
  int fdkey = local_client_fd.release();
  connected_namespaces_.emplace(fdkey, std::move(nsinfo));

  return response;
}

base::ScopedFD Manager::AddLifelineFd(const base::ScopedFD& dbus_fd) {
  if (!dbus_fd.is_valid()) {
    LOG(ERROR) << "Invalid client file descriptor";
    return base::ScopedFD();
  }

  // Dup the client fd into our own: this guarantees that the fd number will
  // be stable and tied to the actual kernel resources used by the client.
  // The duped fd will be watched for read events.
  int fd = dup(dbus_fd.get());
  if (fd < 0) {
    PLOG(ERROR) << "dup() failed";
    return base::ScopedFD();
  }

  lifeline_fd_controllers_[fd] = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&Manager::OnLifelineFdClosed,
                              // The callback will not outlive the object.
                              base::Unretained(this), fd));
  return base::ScopedFD(fd);
}

bool Manager::DeleteLifelineFd(int dbus_fd) {
  auto iter = lifeline_fd_controllers_.find(dbus_fd);
  if (iter == lifeline_fd_controllers_.end()) {
    return false;
  }

  iter->second.reset();  // Destruct the controller, which removes the callback.
  lifeline_fd_controllers_.erase(iter);

  // AddLifelineFd() calls dup(), so this function should close the fd.
  // We still return true since at this point the FileDescriptorWatcher object
  // has been destructed.
  if (IGNORE_EINTR(close(dbus_fd)) < 0) {
    PLOG(ERROR) << "close";
  }

  return true;
}

void Manager::OnLifelineFdClosed(int client_fd) {
  // The process that requested this port has died/exited.
  DeleteLifelineFd(client_fd);

  auto downstream_network_it = downstream_networks_.find(client_fd);
  if (downstream_network_it != downstream_networks_.end()) {
    const auto& info = downstream_network_it->second;
    // Stop IPv6 guest service on the downstream interface if IPv6 is enabled.
    if (info.enable_ipv6 && info.upstream_device) {
      StopForwarding(*info.upstream_device, info.downstream_ifname,
                     ForwardingService::ForwardingSet{.ipv6 = true});
    }

    // Stop the DHCP server if exists.
    // TODO(b/274998094): Currently the DHCPServerController stop the process
    // asynchronously. It might cause the new DHCPServerController creation
    // failure if the new one is created before the process terminated. We
    // should polish the termination procedure to prevent this situation.
    dhcp_server_controllers_.erase(info.downstream_ifname);

    datapath_->StopDownstreamNetwork(info);

    // b/294287313: if the upstream network was created in an ad-hoc
    // fashion through StartTetheringUpstreamNetwork and is not managed by
    // ShillClient, the datapath tear down must also be triggered specially.
    if (!shill_client_->GetDeviceByIfindex(info.upstream_device->ifindex)) {
      StopTetheringUpstreamNetwork(*info.upstream_device);
    }

    LOG(INFO) << "Disconnected Downstream Network " << info;
    downstream_networks_.erase(downstream_network_it);
    return;
  }

  // Remove the rules and IP addresses tied to the lifeline fd.
  auto connected_namespace_it = connected_namespaces_.find(client_fd);
  if (connected_namespace_it != connected_namespaces_.end()) {
    if (connected_namespace_it->second.current_outbound_device) {
      StopForwarding(*connected_namespace_it->second.current_outbound_device,
                     connected_namespace_it->second.host_ifname,
                     ForwardingService::ForwardingSet{.ipv6 = true});
    }
    datapath_->StopRoutingNamespace(connected_namespace_it->second);
    LOG(INFO) << "Disconnected network namespace "
              << connected_namespace_it->second;
    if (connected_namespace_it->second.static_ipv6_config.has_value()) {
      addr_mgr_.ReleaseIPv6Subnet(
          connected_namespace_it->second.static_ipv6_config->host_cidr
              .GetPrefixCIDR());
    }
    // This release the allocated IPv4 subnet.
    connected_namespaces_.erase(connected_namespace_it);
    return;
  }

  auto dns_redirection_it = dns_redirection_rules_.find(client_fd);
  if (dns_redirection_it == dns_redirection_rules_.end()) {
    LOG(ERROR) << "No client_fd found for " << client_fd;
    return;
  }
  auto rule = dns_redirection_it->second;
  datapath_->StopDnsRedirection(rule);
  LOG(INFO) << "Stopped DNS redirection " << rule;
  dns_redirection_rules_.erase(dns_redirection_it);
  // Propagate DNS proxy addresses change.
  if (rule.type == patchpanel::SetDnsRedirectionRuleRequest::ARC) {
    switch (rule.proxy_address.GetFamily()) {
      case net_base::IPFamily::kIPv4:
        dns_proxy_ipv4_addrs_.erase(rule.input_ifname);
        break;
      case net_base::IPFamily::kIPv6:
        dns_proxy_ipv6_addrs_.erase(rule.input_ifname);
        break;
    }
    dbus_client_notifier_->OnNetworkConfigurationChanged();
  }
}

bool Manager::SetDnsRedirectionRule(const SetDnsRedirectionRuleRequest& request,
                                    const base::ScopedFD& client_fd) {
  base::ScopedFD local_client_fd = AddLifelineFd(client_fd);
  if (!local_client_fd.is_valid()) {
    LOG(ERROR) << "Failed to create lifeline fd";
    return false;
  }

  const auto proxy_address =
      net_base::IPAddress::CreateFromString(request.proxy_address());
  if (!proxy_address) {
    LOG(ERROR) << "proxy_address is invalid IP address: "
               << request.proxy_address();
    return false;
  }
  DnsRedirectionRule rule{.type = request.type(),
                          .input_ifname = request.input_ifname(),
                          .proxy_address = *proxy_address,
                          .host_ifname = request.host_ifname()};

  for (const auto& ns : request.nameservers()) {
    const auto nameserver = net_base::IPAddress::CreateFromString(ns);
    if (!nameserver || nameserver->GetFamily() != proxy_address->GetFamily()) {
      LOG(WARNING) << "Invalid nameserver IP address: " << ns;
    } else {
      rule.nameservers.push_back(*nameserver);
    }
  }

  if (!datapath_->StartDnsRedirection(rule)) {
    LOG(ERROR) << "Failed to setup datapath";
    if (!DeleteLifelineFd(local_client_fd.release()))
      LOG(ERROR) << "Failed to delete lifeline fd";
    return false;
  }
  // Notify GuestIPv6Service to add a route for the IPv6 proxy address to the
  // namespace if it did not exist yet, so that the address is reachable.
  if (rule.proxy_address.GetFamily() == net_base::IPFamily::kIPv6) {
    ipv6_svc_->RegisterDownstreamNeighborIP(
        rule.host_ifname, *rule.proxy_address.ToIPv6Address());
  }

  // Propagate DNS proxy addresses change.
  if (rule.type == patchpanel::SetDnsRedirectionRuleRequest::ARC) {
    switch (rule.proxy_address.GetFamily()) {
      case net_base::IPFamily::kIPv4:
        dns_proxy_ipv4_addrs_.emplace(rule.input_ifname,
                                      *rule.proxy_address.ToIPv4Address());
        break;
      case net_base::IPFamily::kIPv6:
        dns_proxy_ipv6_addrs_.emplace(rule.input_ifname,
                                      *rule.proxy_address.ToIPv6Address());
        break;
    }
    dbus_client_notifier_->OnNetworkConfigurationChanged();
  }

  // Store DNS proxy's redirection request.
  int fdkey = local_client_fd.release();
  dns_redirection_rules_.emplace(fdkey, std::move(rule));

  return true;
}

bool Manager::ValidateDownstreamNetworkRequest(
    const DownstreamNetworkInfo& info) {
  // TODO(b/239559602): Validate the request and log any invalid argument:
  //    - |upstream_ifname| should be an active shill Device/Network,
  //    - |downstream_ifname| should not be a shill Device/Network already in
  //    use,
  //    - |downstream_ifname| should not be already in use in another
  //    DownstreamNetworkInfo,
  //    - if there are IPv4 and/or IPv6 configurations, check the prefixes are
  //      correct and available.
  //    - check the downstream subnet doesn't conflict with any IPv4
  //      configuration of the currently connected networks.
  return true;
}

patchpanel::DownstreamNetworkResult Manager::HandleDownstreamNetworkInfo(
    const base::ScopedFD& client_fd, const DownstreamNetworkInfo& info) {
  if (!ValidateDownstreamNetworkRequest(info)) {
    LOG(ERROR) << __func__ << " " << info << ": Invalid request";
    return patchpanel::DownstreamNetworkResult::INVALID_ARGUMENT;
  }

  base::ScopedFD local_client_fd = AddLifelineFd(client_fd);
  if (!local_client_fd.is_valid()) {
    LOG(ERROR) << __func__ << " " << info << ": Failed to create lifeline fd";
    return patchpanel::DownstreamNetworkResult::ERROR;
  }

  if (!datapath_->StartDownstreamNetwork(info)) {
    LOG(ERROR) << __func__ << " " << info
               << ": Failed to configure forwarding to downstream network";
    return patchpanel::DownstreamNetworkResult::DATAPATH_ERROR;
  }

  // Start the DHCP server at downstream.
  if (info.enable_ipv4_dhcp) {
    if (dhcp_server_controllers_.find(info.downstream_ifname) !=
        dhcp_server_controllers_.end()) {
      LOG(ERROR) << __func__ << " " << info
                 << ": DHCP server is already running at "
                 << info.downstream_ifname;
      return patchpanel::DownstreamNetworkResult::INTERFACE_USED;
    }
    const auto config = info.ToDHCPServerConfig();
    if (!config) {
      LOG(ERROR) << __func__ << " " << info
                 << ": Failed to get DHCP server config";
      return patchpanel::DownstreamNetworkResult::INVALID_ARGUMENT;
    }
    auto dhcp_server_controller = std::make_unique<DHCPServerController>(
        metrics_, kTetheringDHCPServerUmaEventMetrics, info.downstream_ifname);
    // TODO(b/274722417): Handle the DHCP server exits unexpectedly.
    if (!dhcp_server_controller->Start(*config, base::DoNothing())) {
      LOG(ERROR) << __func__ << " " << info << ": Failed to start DHCP server";
      return patchpanel::DownstreamNetworkResult::DHCP_SERVER_FAILURE;
    }
    dhcp_server_controllers_[info.downstream_ifname] =
        std::move(dhcp_server_controller);
  }

  // Start IPv6 guest service on the downstream interface if IPv6 is enabled.
  // TODO(b/278966909): Prevents neighbor discovery between the downstream
  // network and other virtual guests and interfaces in the same upstream
  // group.
  if (info.enable_ipv6 && info.upstream_device) {
    StartForwarding(
        *info.upstream_device, info.downstream_ifname,
        ForwardingService::ForwardingSet{.ipv6 = true}, info.mtu,
        CalculateDownstreamCurHopLimit(system_, info.upstream_device->ifname));
  }

  int fdkey = local_client_fd.release();
  downstream_networks_[fdkey] = info;
  return patchpanel::DownstreamNetworkResult::SUCCESS;
}

void Manager::SendGuestMessage(const GuestMessage& msg) {
  ControlMessage cm;
  *cm.mutable_guest_message() = msg;
  adb_proxy_->SendControlMessage(cm);
  mcast_proxy_->SendControlMessage(cm);
}

void Manager::StartForwarding(const ShillClient::Device& shill_device,
                              const std::string& ifname_virtual,
                              const ForwardingService::ForwardingSet& fs,
                              std::optional<int> mtu,
                              std::optional<int> hop_limit) {
  if (shill_device.ifname.empty() || ifname_virtual.empty())
    return;

  if (fs.ipv6) {
    ipv6_svc_->StartForwarding(shill_device, ifname_virtual, mtu, hop_limit);
  }

  if (fs.multicast && IsMulticastInterface(shill_device.ifname)) {
    ControlMessage cm;
    DeviceMessage* msg = cm.mutable_device_message();
    msg->set_dev_ifname(shill_device.ifname);
    msg->set_br_ifname(ifname_virtual);

    LOG(INFO) << "Starting multicast forwarding from " << shill_device << " to "
              << ifname_virtual;
    mcast_proxy_->SendControlMessage(cm);
  }
}

void Manager::StopForwarding(const ShillClient::Device& shill_device,
                             const std::string& ifname_virtual,
                             const ForwardingService::ForwardingSet& fs) {
  if (shill_device.ifname.empty())
    return;

  if (fs.ipv6) {
    if (ifname_virtual.empty()) {
      ipv6_svc_->StopUplink(shill_device);
    } else {
      ipv6_svc_->StopForwarding(shill_device, ifname_virtual);
    }
  }

  if (fs.multicast) {
    ControlMessage cm;
    DeviceMessage* msg = cm.mutable_device_message();
    msg->set_dev_ifname(shill_device.ifname);
    msg->set_teardown(true);
    if (!ifname_virtual.empty()) {
      msg->set_br_ifname(ifname_virtual);
    }
    if (ifname_virtual.empty()) {
      LOG(INFO) << "Stopping multicast forwarding on " << shill_device;
    } else {
      LOG(INFO) << "Stopping multicast forwarding from " << shill_device
                << " to " << ifname_virtual;
    }
    mcast_proxy_->SendControlMessage(cm);
  }
}

void Manager::NotifyAndroidWifiMulticastLockChange(bool is_held) {
  // When multicast lock status changes from not held to held or the other
  // way, decide whether to enable or disable multicast forwarder for ARC.
  if (arc_svc_->is_android_wifi_multicast_lock_held() == is_held) {
    return;
  }

  // If arc is not interactive, multicast lock held status does not
  // affect multicast traffic.
  arc_svc_->set_android_wifi_multicast_lock_held(is_held);
  if (!arc_svc_->is_arc_interactive()) {
    return;
  }

  // Only start/stop forwarding when multicast allowed status changes to avoid
  // start/stop forwarding multiple times, also wifi multicast lock should
  // only affect multicast traffic on wireless device.
  for (const auto* arc_device : arc_svc_->GetDevices()) {
    // The "arc0" ARC device is ignored.
    if (!arc_device->shill_device_ifname()) {
      continue;
    }
    auto* upstream_shill_device = shill_client_->GetDeviceByShillDeviceName(
        *arc_device->shill_device_ifname());
    if (!upstream_shill_device) {
      LOG(ERROR) << __func__
                 << ": no upstream shill Device found for ARC Device "
                 << *arc_device;
      continue;
    }
    if (upstream_shill_device->type != ShillClient::Device::Type::kWifi) {
      continue;
    }
    if (arc_svc_->is_android_wifi_multicast_lock_held()) {
      StartForwarding(*upstream_shill_device, arc_device->bridge_ifname(),
                      ForwardingService::ForwardingSet{.multicast = true});
    } else {
      StopForwarding(*upstream_shill_device, arc_device->bridge_ifname(),
                     ForwardingService::ForwardingSet{.multicast = true});
    }
  }

  // Notify multicast metrics for forwarder state change.
  if (arc_svc_->is_android_wifi_multicast_lock_held()) {
    multicast_metrics_->OnARCWiFiForwarderStarted();
  } else {
    multicast_metrics_->OnARCWiFiForwarderStopped();
  }
}

void Manager::NotifyAndroidInteractiveState(bool is_interactive) {
  // When power state of device changes, decide whether to disable
  // multicast forwarder for ARC.
  if (arc_svc_->is_arc_interactive() == is_interactive) {
    return;
  }

  // If ARC power state has changed to interactive, enable all
  // interfaces that are not wifi interface, and only enable wifi interfaces
  // when wifi multicast lock is held.
  // If ARC power state has changed to non-interactive, disable all
  // interfaces that are not wifi interface, and only disable wifi
  // interfaces when they were in enabled state (multicast lock held).
  arc_svc_->set_arc_interactive(is_interactive);
  for (const auto* arc_device : arc_svc_->GetDevices()) {
    // The "arc0" ARC device is ignored.
    if (!arc_device->shill_device_ifname()) {
      continue;
    }
    auto* upstream_shill_device = shill_client_->GetDeviceByShillDeviceName(
        *arc_device->shill_device_ifname());
    if (!upstream_shill_device) {
      LOG(ERROR) << __func__
                 << ": no upstream shill Device found for ARC Device "
                 << *arc_device;
      continue;
    }
    if (upstream_shill_device->type == ShillClient::Device::Type::kWifi &&
        !arc_svc_->is_android_wifi_multicast_lock_held()) {
      continue;
    }
    if (arc_svc_->is_arc_interactive()) {
      StartForwarding(*upstream_shill_device, arc_device->bridge_ifname(),
                      ForwardingService::ForwardingSet{.multicast = true});
    } else {
      StopForwarding(*upstream_shill_device, arc_device->bridge_ifname(),
                     ForwardingService::ForwardingSet{.multicast = true});
    }
  }

  // Notify multicast metrics for forwarder state change.
  if (!arc_svc_->is_android_wifi_multicast_lock_held()) {
    return;
  }
  if (arc_svc_->is_arc_interactive()) {
    multicast_metrics_->OnARCWiFiForwarderStarted();
  } else {
    multicast_metrics_->OnARCWiFiForwarderStopped();
  }
}

void Manager::NotifySocketConnectionEvent(
    const NotifySocketConnectionEventRequest& request) {
  if (!request.has_msg()) {
    LOG(ERROR)
        << ": no message attached in socket connection event notification.";
    return;
  }
  qos_svc_->ProcessSocketConnectionEvent(request.msg());
}

bool Manager::SetFeatureFlag(
    patchpanel::SetFeatureFlagRequest::FeatureFlag flag, bool enabled) {
  bool old_flag = false;
  switch (flag) {
    case patchpanel::SetFeatureFlagRequest::FeatureFlag::
        SetFeatureFlagRequest_FeatureFlag_WIFI_QOS:
      old_flag = qos_svc_->is_enabled();
      if (enabled) {
        qos_svc_->Enable();
      } else {
        qos_svc_->Disable();
      }
      break;
    case patchpanel::SetFeatureFlagRequest::FeatureFlag::
        SetFeatureFlagRequest_FeatureFlag_CLAT:
      old_flag = clat_svc_->is_enabled();
      if (enabled) {
        clat_svc_->Enable();
      } else {
        clat_svc_->Disable();
      }
      break;
    default:
      LOG(ERROR) << __func__ << "Unknown feature flag: " << flag;
      break;
  }
  return old_flag;
}

std::optional<int> Manager::CalculateDownstreamCurHopLimit(
    System* system, const std::string& upstream_iface) {
  const std::string content =
      system->SysNetGet(System::SysNet::kIPv6HopLimit, upstream_iface);
  int value = 0;
  if (!base::StringToInt(content, &value)) {
    LOG(ERROR) << "Failed to convert `" << content << "` to int";
    return std::nullopt;
  }

  // The CurHopLimit of downstream should be the value of upstream minus 1.
  value -= 1;
  if (value < 0 || value > 255) {
    LOG(ERROR) << "The value of CurHopLimit is invalid: " << value;
    return std::nullopt;
  }

  return value;
}

}  // namespace patchpanel
