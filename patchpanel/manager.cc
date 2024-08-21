// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/manager.h"

#include <optional>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/cxx23_to_underlying.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/process_manager.h>
#include <chromeos/net-base/technology.h>
#include <patchpanel/proto_bindings/traffic_annotation.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/crostini_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/downstream_network_service.h"
#include "patchpanel/multicast_metrics.h"
#include "patchpanel/net_util.h"
#include "patchpanel/network/network_applier.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/qos_service.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
// Delay to restart IPv6 in a namespace to trigger SLAAC in the kernel.
constexpr int kIPv6RestartDelayMs = 300;

// Types of conntrack events ConntrackMonitor handles. Listeners added to the
// monitor can only listen to types of events included in this list.
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew};

#if USE_ARCVM
constexpr ArcService::ArcType kArcType = ArcService::ArcType::kVM;
#else
constexpr ArcService::ArcType kArcType = ArcService::ArcType::kContainer;
#endif

}  // namespace

MulticastForwardingControlMessage::Direction
GetMulticastControlMessageDirection(MulticastForwarder::Direction dir) {
  switch (dir) {
    case MulticastForwarder::Direction::kInboundOnly:
      return MulticastForwardingControlMessage::INBOUND_ONLY;
    case MulticastForwarder::Direction::kOutboundOnly:
      return MulticastForwardingControlMessage::OUTBOUND_ONLY;
    case MulticastForwarder::Direction::kTwoWays:
      return MulticastForwardingControlMessage::TWO_WAYS;
  }
}

Manager::Manager(const base::FilePath& cmd_path,
                 System* system,
                 net_base::ProcessManager* process_manager,
                 MetricsLibraryInterface* metrics,
                 DbusClientNotifier* dbus_client_notifier,
                 std::unique_ptr<ShillClient> shill_client,
                 std::unique_ptr<RTNLClient> rtnl_client)
    : system_(system),
      metrics_(metrics),
      dbus_client_notifier_(dbus_client_notifier),
      shill_client_(std::move(shill_client)),
      rtnl_client_(std::move(rtnl_client)),
      adb_proxy_(system, process_manager, cmd_path, "--adb_proxy_fd"),
      mcast_proxy_(system, process_manager, cmd_path, "--mcast_proxy_fd"),
      nd_proxy_(system, process_manager, cmd_path, "--nd_proxy_fd"),
      socket_service_(system, process_manager, cmd_path, "--socket_service_fd"),
      datapath_(system),
      routing_svc_(system, &lifeline_fd_svc_),
      conntrack_monitor_(kConntrackEvents),
      counters_svc_(&datapath_, &conntrack_monitor_),
      multicast_counters_svc_(&datapath_),
      multicast_metrics_(&multicast_counters_svc_, metrics),
      ipv6_svc_(&nd_proxy_, &datapath_, system),
      qos_svc_(&datapath_, &conntrack_monitor_),
      downstream_network_svc_(DownstreamNetworkService(metrics_,
                                                       system_,
                                                       &datapath_,
                                                       &routing_svc_,
                                                       this,
                                                       rtnl_client_.get(),
                                                       &lifeline_fd_svc_,
                                                       shill_client_.get(),
                                                       &ipv6_svc_,
                                                       &counters_svc_)),
      arc_svc_(kArcType,
               &datapath_,
               &addr_mgr_,
               this,
               metrics_,
               dbus_client_notifier_),
      cros_svc_(&addr_mgr_, &datapath_, this, dbus_client_notifier_),
      network_monitor_svc_(base::BindRepeating(
          &Manager::OnNeighborReachabilityEvent, weak_factory_.GetWeakPtr())),
      clat_svc_(&datapath_, process_manager, system) {
  DCHECK(rtnl_client_);

  multicast_metrics_.Start(MulticastMetrics::Type::kTotal);

  // Setups the RTNL socket and listens to neighbor events. This should be
  // called before NetworkMonitorService::Start and NetworkApplier::Start.
  // RTMGRP_NEIGH is needed by NetworkMonitorService.
  net_base::RTNLHandler::GetInstance()->Start(RTMGRP_NEIGH);

  // TODO(b/293997937): NetworkApplier to be a Manager-owned service rather than
  // a singleton.
  NetworkApplier::GetInstance()->Start();

  // Post a delayed task to run the delayed initialization which may take time
  // but not necessary for handling dbus methods. There are two main purposes
  // here:
  // 1) Make patchpanel D-Bus service ready as early as possible.
  // 2) Specifically we want to handle the ConfigureNetwork() request as early
  //    as possible which is critical to basic network connectivity.
  //
  // The delay value (1 second) is selected arbitrarily.
  //
  // Caveats:
  // - It's possible that ConfigureNetwork() request doesn't come in in the
  //   timeout, and thus this logic actually delayed its execution by at most 1
  //   second.
  // - The tasks in RunDelayedInitialization() is just not critical to handling
  //   D-Bus request but still critical to the full connectivity, so we may
  //   waste some time which can be used to set it up.
  //
  // Ideally what we want to do here is to schedule a low-priority task with
  // deadline, which can not be implemented with a very easy way now.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Manager::RunDelayedInitialization,
                     weak_factory_.GetWeakPtr()),
      base::Seconds(1));
}

Manager::~Manager() {
  // Tear down any remaining ConnectNamespace setup.
  std::vector<int> connected_namespaces_ifnames;
  for (const auto& [id, _] : connected_namespaces_) {
    connected_namespaces_ifnames.push_back(id);
  }
  for (int id : connected_namespaces_ifnames) {
    OnConnectedNamespaceAutoclose(id);
  }

  // Tear down any remaining DNS redirection rule setup.
  std::vector<int> dns_proxy_lifelines_fds;
  for (const auto& [lifeline_fd, _] : dns_redirection_rules_) {
    dns_proxy_lifelines_fds.push_back(lifeline_fd);
  }
  for (int lifeline_fd : dns_proxy_lifelines_fds) {
    OnDnsRedirectionRulesAutoclose(lifeline_fd);
  }

  // Invalidate the WeakPtr before destroying member services.
  weak_factory_.InvalidateWeakPtrs();
}

void Manager::RunDelayedInitialization() {
  LOG(INFO) << __func__ << ": start";

  shill_client_->RegisterDevicesChangedHandler(base::BindRepeating(
      &Manager::OnShillDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterIPConfigsChangedHandler(base::BindRepeating(
      &Manager::OnIPConfigsChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterIPv6NetworkChangedHandler(base::BindRepeating(
      &Manager::OnIPv6NetworkChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDoHProvidersChangedHandler(base::BindRepeating(
      &Manager::OnDoHProvidersChanged, weak_factory_.GetWeakPtr()));

  // Make sure patchpanel get aware of the Devices created before it starts.
  shill_client_->ScanDevices();

  // Shill client's RegisterDefault*DeviceChangedHandler methods trigger the
  // Manager's callbacks on registration. Call them after everything is set up.
  shill_client_->RegisterDefaultLogicalDeviceChangedHandler(
      base::BindRepeating(&Manager::OnShillDefaultLogicalDeviceChanged,
                          weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDefaultPhysicalDeviceChangedHandler(
      base::BindRepeating(&Manager::OnShillDefaultPhysicalDeviceChanged,
                          weak_factory_.GetWeakPtr()));

  LOG(INFO) << __func__ << ": finished";
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

  if (prev_device && prev_device->technology == net_base::Technology::kVPN) {
    datapath_.StopVpnRouting(*prev_device);
    counters_svc_.OnVpnDeviceRemoved(prev_device->ifname);
  }

  if (new_device && new_device->technology == net_base::Technology::kVPN) {
    counters_svc_.OnVpnDeviceAdded(new_device->ifname);
    datapath_.StartVpnRouting(*new_device);
  }

  cros_svc_.OnShillDefaultLogicalDeviceChanged(new_device, prev_device);

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
      StopIPv6NDPForwarding(*prev_device, nsinfo.host_ifname);
    }
    if (new_device) {
      StartIPv6NDPForwarding(*new_device, nsinfo.host_ifname);

      // Disable and re-enable IPv6. This is necessary to trigger SLAAC in the
      // kernel to send RS. Add a delay for the forwarding to be set up.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                         nsinfo.netns_name),
          base::Milliseconds(kIPv6RestartDelayMs));
    }
  }
  clat_svc_.OnShillDefaultLogicalDeviceChanged(new_device, prev_device);
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
      StopIPv6NDPForwarding(*prev_device, nsinfo.host_ifname);
    }
    if (new_device) {
      StartIPv6NDPForwarding(*new_device, nsinfo.host_ifname);

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

void Manager::RestartIPv6(std::string_view netns_name) {
  auto ns = ScopedNS::EnterNetworkNS(netns_name);
  if (!ns) {
    LOG(ERROR) << "Invalid namespace name " << netns_name;
    return;
  }

  datapath_.RestartIPv6();
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
      StopIPv6NDPForwarding(device, nsinfo.host_ifname);
    }
    StopIPv6NDPForwarding(device, /*ifname_virtual=*/"");
    StopMulticastForwarding(device, /*ifname_virtual=*/"");
    StopBroadcastForwarding(device, /*ifname_virtual=*/"");
    datapath_.StopConnectionPinning(device);
    datapath_.RemoveRedirectDnsRule(device);
    arc_svc_.RemoveDevice(device);
    multicast_metrics_.OnPhysicalDeviceRemoved(device);
    counters_svc_.OnPhysicalDeviceRemoved(device.ifname);
    multicast_counters_svc_.OnPhysicalDeviceRemoved(device);
    qos_svc_.OnPhysicalDeviceRemoved(device);

    datapath_.StopSourceIPv6PrefixEnforcement(device);
  }

  for (const auto& device : added) {
    qos_svc_.OnPhysicalDeviceAdded(device);
    counters_svc_.OnPhysicalDeviceAdded(device.ifname);
    multicast_counters_svc_.OnPhysicalDeviceAdded(device);
    multicast_metrics_.OnPhysicalDeviceAdded(device);
    for (auto& [_, nsinfo] : connected_namespaces_) {
      if (nsinfo.outbound_ifname != device.ifname) {
        continue;
      }
      if (nsinfo.static_ipv6_config.has_value()) {
        continue;
      }
      StartIPv6NDPForwarding(device, nsinfo.host_ifname);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                         nsinfo.netns_name),
          base::Milliseconds(kIPv6RestartDelayMs));
    }
    datapath_.StartConnectionPinning(device);

    // AddRedirectDnsRule to the first IPv4 DNS.
    for (const auto& dns : device.network_config.dns_servers) {
      if (dns.GetFamily() == net_base::IPFamily::kIPv4) {
        datapath_.AddRedirectDnsRule(device, dns.ToString());
        break;
      }
    }

    arc_svc_.AddDevice(device);

    datapath_.StartSourceIPv6PrefixEnforcement(device);
    if (!device.network_config.ipv6_addresses.empty()) {
      // TODO(b/279871350): Support prefix shorter than /64.
      const auto prefix = GuestIPv6Service::IPAddressTo64BitPrefix(
          device.network_config.ipv6_addresses[0].address());
      datapath_.UpdateSourceEnforcementIPv6Prefix(device, prefix);
    }
  }

  network_monitor_svc_.OnShillDevicesChanged(added, removed);
}

void Manager::OnIPConfigsChanged(const ShillClient::Device& shill_device) {
  // AddRedirectDnsRule to the first IPv4 DNS, or RemoveRedirectDnsRule if
  // there's no IPv4 DNS.
  bool has_ipv4_dns = false;
  for (const auto& dns : shill_device.network_config.dns_servers) {
    if (dns.GetFamily() == net_base::IPFamily::kIPv4) {
      datapath_.AddRedirectDnsRule(shill_device, dns.ToString());
      has_ipv4_dns = true;
      break;
    }
  }
  if (!has_ipv4_dns) {
    datapath_.RemoveRedirectDnsRule(shill_device);
  }

  multicast_metrics_.OnIPConfigsChanged(shill_device);
  ipv6_svc_.UpdateUplinkIPv6DNS(shill_device);

  // Update local copies of the ShillClient::Device to keep IP configuration
  // properties in sync.
  for (auto& [_, nsinfo] : connected_namespaces_) {
    if (!nsinfo.current_outbound_device) {
      continue;
    }
    if (nsinfo.current_outbound_device->ifname == shill_device.ifname) {
      nsinfo.current_outbound_device = shill_device;
    }
  }

  downstream_network_svc_.UpdateDeviceIPConfig(shill_device);
  arc_svc_.UpdateDeviceIPConfig(shill_device);

  const auto* default_logical_device = shill_client_->default_logical_device();
  if (default_logical_device &&
      shill_device.ifname == default_logical_device->ifname) {
    clat_svc_.OnDefaultLogicalDeviceIPConfigChanged(shill_device);
  }

  if (!shill_device.IsConnected()) {
    qos_svc_.OnPhysicalDeviceDisconnected(shill_device);
  }

  network_monitor_svc_.OnIPConfigsChanged(shill_device);
}

void Manager::OnIPv6NetworkChanged(const ShillClient::Device& shill_device) {
  ipv6_svc_.OnUplinkIPv6Changed(shill_device);

  if (shill_device.network_config.ipv6_addresses.empty()) {
    datapath_.UpdateSourceEnforcementIPv6Prefix(shill_device, std::nullopt);
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

  // TODO(b/279871350): Support prefix shorter than /64.
  const auto prefix = GuestIPv6Service::IPAddressTo64BitPrefix(
      shill_device.network_config.ipv6_addresses[0].address());
  datapath_.UpdateSourceEnforcementIPv6Prefix(shill_device, prefix);
}

void Manager::OnDoHProvidersChanged(
    const ShillClient::DoHProviders& doh_providers) {
  qos_svc_.UpdateDoHProviders(doh_providers);
}

bool Manager::ArcStartup(pid_t pid) {
  if (pid < 0) {
    LOG(ERROR) << "Invalid ARC pid: " << pid;
    return false;
  }

  if (!arc_svc_.Start(static_cast<uint32_t>(pid)))
    return false;

  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC);
  msg.set_arc_pid(pid);
  SendGuestMessage(msg);

  multicast_metrics_.OnARCStarted();

  return true;
}

void Manager::ArcShutdown() {
  multicast_metrics_.OnARCStopped();

  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC);
  SendGuestMessage(msg);

  // After the ARC container has stopped, the pid is not known anymore.
  // The pid argument is ignored by ArcService.
  arc_svc_.Stop(0);
}

std::optional<patchpanel::ArcVmStartupResponse> Manager::ArcVmStartup(
    uint32_t cid) {
  if (!arc_svc_.Start(cid)) {
    return std::nullopt;
  }
  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC_VM);
  msg.set_arcvm_vsock_cid(cid);
  SendGuestMessage(msg);

  multicast_metrics_.OnARCStarted();

  patchpanel::ArcVmStartupResponse response;
  if (const auto arc0_addr = arc_svc_.GetArc0IPv4Address()) {
    response.set_arc0_ipv4_address(arc0_addr->ToByteString());
  }
  // Only pass static tap devices before ARCVM starts. Hotplugged devices, if
  // any, are added after VM starts.
  for (const auto& tap : arc_svc_.GetStaticTapDevices()) {
    response.add_tap_device_ifnames(tap);
  }
  return response;
}

void Manager::ArcVmShutdown(uint32_t cid) {
  multicast_metrics_.OnARCStopped();

  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC_VM);
  msg.set_arcvm_vsock_cid(cid);
  SendGuestMessage(msg);

  arc_svc_.Stop(cid);
}

const CrostiniService::CrostiniDevice* Manager::StartCrosVm(
    uint64_t vm_id, CrostiniService::VMType vm_type, uint32_t subnet_index) {
  const auto* guest_device = cros_svc_.Start(vm_id, vm_type, subnet_index);
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
  cros_svc_.Stop(vm_id);
}

GetDevicesResponse Manager::GetDevices() const {
  GetDevicesResponse response;

  for (const auto* arc_device : arc_svc_.GetDevices()) {
    // The legacy "arc0" Device is never exposed in "GetDevices".
    if (!arc_device->shill_device_ifname()) {
      continue;
    }
    auto* dev = response.add_devices();
    arc_device->ConvertToProto(dev);
    FillArcDeviceDnsProxyProto(*arc_device, dev, dns_proxy_ipv4_addrs_,
                               dns_proxy_ipv6_addrs_);
  }

  for (const auto* crostini_device : cros_svc_.GetDevices()) {
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

const CrostiniService::CrostiniDevice* const Manager::BorealisVmStartup(
    uint64_t vm_id) {
  const auto* guest_device =
      StartCrosVm(vm_id, CrostiniService::VMType::kBorealis);
  if (!guest_device) {
    LOG(ERROR) << "Failed to start Borealis VM network service";
    return nullptr;
  }
  qos_svc_.OnBorealisVMStarted(guest_device->tap_device_ifname());
  return guest_device;
}

void Manager::BorealisVmShutdown(uint64_t vm_id) {
  const CrostiniService::CrostiniDevice* guest_device =
      cros_svc_.GetDevice(vm_id);
  if (guest_device) {
    qos_svc_.OnBorealisVMStopped(guest_device->tap_device_ifname());
  }
  StopCrosVm(vm_id, CrostiniService::VMType::kBorealis);
}

std::map<CountersService::CounterKey, CountersService::Counter>
Manager::GetTrafficCounters(const std::set<std::string>& shill_devices) const {
  return counters_svc_.GetCounters(shill_devices);
}

bool Manager::ModifyPortRule(const ModifyPortRuleRequest& request) {
  return datapath_.ModifyPortRule(request);
}

void Manager::SetVpnLockdown(bool enable_vpn_lockdown) {
  datapath_.SetVpnLockdown(enable_vpn_lockdown);
}

bool Manager::TagSocket(const patchpanel::TagSocketRequest& request,
                        const base::ScopedFD& socket_fd) {
  std::optional<int> network_id = std::nullopt;
  if (request.has_network_id()) {
    network_id = request.network_id();
  }

  auto policy = VPNRoutingPolicy::kDefault;
  switch (request.vpn_policy()) {
    case TagSocketRequest::DEFAULT_ROUTING:
      policy = VPNRoutingPolicy::kDefault;
      break;
    case TagSocketRequest::ROUTE_ON_VPN:
      policy = VPNRoutingPolicy::kRouteOnVPN;
      break;
    case TagSocketRequest::BYPASS_VPN:
      policy = VPNRoutingPolicy::kBypassVPN;
      break;
    default:
      LOG(ERROR) << __func__ << ": Invalid vpn policy value"
                 << request.vpn_policy();
      return false;
  }

  std::optional<TrafficAnnotationId> annotation_id;
  if (request.has_traffic_annotation()) {
    switch (request.traffic_annotation().host_id()) {
      case traffic_annotation::TrafficAnnotation::UNSPECIFIED:
        annotation_id = TrafficAnnotationId::kUnspecified;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_PORTAL_DETECTOR:
        annotation_id = TrafficAnnotationId::kShillPortalDetector;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_CAPPORT_CLIENT:
        annotation_id = TrafficAnnotationId::kShillCapportClient;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_CARRIER_ENTITLEMENT:
        annotation_id = TrafficAnnotationId::kShillCarrierEntitlement;
        break;
      default:
        LOG(ERROR) << __func__ << ": Invalid traffic annotation id "
                   << request.traffic_annotation().host_id();
        return false;
    }
  }

  return routing_svc_.TagSocket(socket_fd.get(), network_id, policy,
                                annotation_id);
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
    const ConnectNamespaceRequest& request, base::ScopedFD client_fd) {
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

  auto cancel_lifeline_fd = lifeline_fd_svc_.AddLifelineFD(
      std::move(client_fd),
      base::BindOnce(&Manager::OnConnectedNamespaceAutoclose,
                     weak_factory_.GetWeakPtr(),
                     connected_namespaces_next_id_));
  if (!cancel_lifeline_fd) {
    LOG(ERROR) << "Failed to create lifeline fd";
    return response;
  }

  const std::string ifname_id = std::to_string(connected_namespaces_next_id_);
  ConnectedNamespace nsinfo = {};
  nsinfo.pid = request.pid();
  nsinfo.netns_name = "connected_netns_" + ifname_id;
  nsinfo.source = ProtoToTrafficSource(request.traffic_source());
  if (nsinfo.source == TrafficSource::kUnknown) {
    nsinfo.source = TrafficSource::kSystem;
  }
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

  if (!datapath_.StartRoutingNamespace(nsinfo)) {
    LOG(ERROR) << "Failed to setup datapath";
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
    StartIPv6NDPForwarding(*current_outbound_device, nsinfo.host_ifname);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Manager::RestartIPv6, weak_factory_.GetWeakPtr(),
                       nsinfo.netns_name),
        base::Milliseconds(kIPv6RestartDelayMs));
  }

  // Store ConnectedNamespace
  nsinfo.cancel_lifeline_fd = std::move(cancel_lifeline_fd);
  connected_namespaces_.emplace(connected_namespaces_next_id_,
                                std::move(nsinfo));
  connected_namespaces_next_id_++;
  return response;
}

void Manager::OnConnectedNamespaceAutoclose(int connected_namespace_id) {
  // Remove the rules and IP addresses tied to the lifeline fd.
  auto connected_namespace_it =
      connected_namespaces_.find(connected_namespace_id);
  if (connected_namespace_it == connected_namespaces_.end()) {
    return;
  }

  LOG(INFO) << __func__ << ": " << connected_namespace_it->second;

  if (connected_namespace_it->second.current_outbound_device) {
    StopIPv6NDPForwarding(
        *connected_namespace_it->second.current_outbound_device,
        connected_namespace_it->second.host_ifname);
  }
  datapath_.StopRoutingNamespace(connected_namespace_it->second);
  if (connected_namespace_it->second.static_ipv6_config.has_value()) {
    addr_mgr_.ReleaseIPv6Subnet(
        connected_namespace_it->second.static_ipv6_config->host_cidr
            .GetPrefixCIDR());
  }
  // This release the allocated IPv4 subnet.
  connected_namespaces_.erase(connected_namespace_it);
}

void Manager::OnDnsRedirectionRulesAutoclose(int lifeline_fd) {
  auto dns_redirection_it = dns_redirection_rules_.find(lifeline_fd);
  if (dns_redirection_it == dns_redirection_rules_.end()) {
    return;
  }

  auto rule = std::move(dns_redirection_it->second);
  LOG(INFO) << __func__ << ": " << rule;

  datapath_.StopDnsRedirection(rule);
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
                                    base::ScopedFD client_fd) {
  int lifeline_fd = client_fd.get();
  auto cancel_lifeline_fd = lifeline_fd_svc_.AddLifelineFD(
      std::move(client_fd),
      base::BindOnce(&Manager::OnDnsRedirectionRulesAutoclose,
                     weak_factory_.GetWeakPtr(), lifeline_fd));
  if (!cancel_lifeline_fd) {
    LOG(ERROR) << __func__ << ": Failed to create lifeline fd";
    return false;
  }

  const auto proxy_address =
      net_base::IPAddress::CreateFromString(request.proxy_address());
  if (!proxy_address) {
    LOG(ERROR) << __func__ << ": proxy_address is invalid IP address: "
               << request.proxy_address();
    return false;
  }
  DnsRedirectionRule rule{.type = request.type(),
                          .input_ifname = request.input_ifname(),
                          .proxy_address = *proxy_address,
                          .host_ifname = request.host_ifname(),
                          .cancel_lifeline_fd = std::move(cancel_lifeline_fd)};

  for (const auto& ns : request.nameservers()) {
    const auto nameserver = net_base::IPAddress::CreateFromString(ns);
    if (!nameserver || nameserver->GetFamily() != proxy_address->GetFamily()) {
      LOG(WARNING) << __func__ << ": Invalid nameserver IP address: " << ns;
    } else {
      rule.nameservers.push_back(*nameserver);
    }
  }

  if (!datapath_.StartDnsRedirection(rule)) {
    LOG(ERROR) << __func__ << ": Failed to setup datapath";
    return false;
  }
  // Notify GuestIPv6Service to add a route for the IPv6 proxy address to the
  // namespace if it did not exist yet, so that the address is reachable.
  if (rule.proxy_address.GetFamily() == net_base::IPFamily::kIPv6) {
    ipv6_svc_.RegisterDownstreamNeighborIP(rule.host_ifname,
                                           *rule.proxy_address.ToIPv6Address());
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
  dns_redirection_rules_.emplace(lifeline_fd, std::move(rule));
  return true;
}

void Manager::SendGuestMessage(const GuestMessage& msg) {
  ControlMessage cm;
  *cm.mutable_guest_message() = msg;
  adb_proxy_.SendControlMessage(cm);
  mcast_proxy_.SendControlMessage(cm);
}

void Manager::StartIPv6NDPForwarding(const ShillClient::Device& shill_device,
                                     std::string_view ifname_virtual,
                                     std::optional<int> mtu,
                                     std::optional<int> hop_limit) {
  if (shill_device.ifname.empty() || ifname_virtual.empty()) {
    return;
  }

  // TODO(b/325359902): Change GuestIPv6Service interface to take
  // std::string_view and delete conversion here.
  ipv6_svc_.StartForwarding(shill_device, std::string(ifname_virtual), mtu,
                            hop_limit);
}

void Manager::StopIPv6NDPForwarding(const ShillClient::Device& shill_device,
                                    std::string_view ifname_virtual) {
  if (shill_device.ifname.empty()) {
    return;
  }

  if (ifname_virtual.empty()) {
    ipv6_svc_.StopUplink(shill_device);
  } else {
    // TODO(b/325359902): Change GuestIPv6Service interface to take
    // std::string_view and delete conversion here.
    ipv6_svc_.StopForwarding(shill_device, std::string(ifname_virtual));
  }
}

void Manager::StartBroadcastForwarding(const ShillClient::Device& shill_device,
                                       std::string_view ifname_virtual) {
  if (shill_device.ifname.empty() || ifname_virtual.empty()) {
    return;
  }

  LOG(INFO) << "Starting broadcast forwarding from " << shill_device << " to "
            << ifname_virtual;
  ControlMessage cm;
  BroadcastForwardingControlMessage* msg = cm.mutable_bcast_control();
  msg->set_lan_ifname(shill_device.ifname);
  msg->set_int_ifname(ifname_virtual);
  mcast_proxy_.SendControlMessage(cm);
}

void Manager::StopBroadcastForwarding(const ShillClient::Device& shill_device,
                                      std::string_view ifname_virtual) {
  if (shill_device.ifname.empty()) {
    return;
  }

  if (ifname_virtual.empty()) {
    LOG(INFO) << "Stopping broadcast forwarding on " << shill_device;
  } else {
    LOG(INFO) << "Stopping broadcast forwarding from " << shill_device << " to "
              << ifname_virtual;
  }

  ControlMessage cm;
  BroadcastForwardingControlMessage* msg = cm.mutable_bcast_control();
  msg->set_lan_ifname(shill_device.ifname);
  msg->set_teardown(true);
  if (!ifname_virtual.empty()) {
    msg->set_int_ifname(ifname_virtual);
  }
  mcast_proxy_.SendControlMessage(cm);
}

void Manager::StartMulticastForwarding(const ShillClient::Device& shill_device,
                                       std::string_view ifname_virtual,
                                       MulticastForwarder::Direction dir) {
  if (shill_device.ifname.empty() || ifname_virtual.empty()) {
    return;
  }

  if (!IsMulticastInterface(shill_device.ifname)) {
    return;
  }

  LOG(INFO) << "Starting multicast forwarding from " << shill_device << " to "
            << ifname_virtual;
  ControlMessage cm;
  MulticastForwardingControlMessage* msg = cm.mutable_mcast_control();
  msg->set_lan_ifname(shill_device.ifname);
  msg->set_int_ifname(ifname_virtual);
  msg->set_dir(GetMulticastControlMessageDirection(dir));
  mcast_proxy_.SendControlMessage(cm);
}

void Manager::StopMulticastForwarding(const ShillClient::Device& shill_device,
                                      std::string_view ifname_virtual,
                                      MulticastForwarder::Direction dir) {
  if (shill_device.ifname.empty())
    return;

  if (ifname_virtual.empty()) {
    LOG(INFO) << "Stopping multicast forwarding on " << shill_device;
  } else {
    LOG(INFO) << "Stopping multicast forwarding from " << shill_device << " to "
              << ifname_virtual;
  }

  ControlMessage cm;
  MulticastForwardingControlMessage* msg = cm.mutable_mcast_control();
  msg->set_lan_ifname(shill_device.ifname);
  msg->set_teardown(true);
  if (!ifname_virtual.empty()) {
    msg->set_int_ifname(ifname_virtual);
  }
  msg->set_dir(GetMulticastControlMessageDirection(dir));
  mcast_proxy_.SendControlMessage(cm);
}

void Manager::ConfigureNetwork(int ifindex,
                               const std::string& ifname,
                               NetworkApplier::Area area,
                               const net_base::NetworkConfig& network_config,
                               net_base::NetworkPriority priority,
                               NetworkApplier::Technology technology) {
  LOG(INFO) << __func__ << " on " << ifname << "(" << ifindex
            << "): " << network_config << ", priority " << priority
            << ", area 0x" << std::hex << base::to_underlying(area);

  NetworkApplier::GetInstance()->ApplyNetworkConfig(
      ifindex, ifname, area, network_config, priority, technology);

  // TODO(b/293997937): Move dynamic iptables rule setup here.

  // Updating network config in patchpanel is not very cheap since it may invoke
  // a lot of iptables executions. We need to do this here because in some
  // cases, after this function returns, shill will turn the network into the
  // connected state, and the network validation may start after that, which
  // will require some iptables configuration to work. Also see
  // http://b/348602073#comment12.
  if (area == NetworkApplier::Area::kClear) {
    shill_client_->ClearNetworkConfigCache(ifindex);
  } else {
    shill_client_->UpdateNetworkConfigCache(ifindex, network_config);
  }
}

void Manager::NotifyAndroidWifiMulticastLockChange(bool is_held) {
  auto before = arc_svc_.IsWiFiMulticastForwardingRunning();
  arc_svc_.NotifyAndroidWifiMulticastLockChange(is_held);
  auto after = arc_svc_.IsWiFiMulticastForwardingRunning();
  if (!before && after) {
    multicast_metrics_.OnARCWiFiForwarderStarted();
  } else if (before && !after) {
    multicast_metrics_.OnARCWiFiForwarderStopped();
  }
}

void Manager::NotifySocketConnectionEvent(
    const NotifySocketConnectionEventRequest& request) {
  if (!request.has_msg()) {
    LOG(ERROR) << __func__ << ": no message attached.";
    return;
  }
  qos_svc_.ProcessSocketConnectionEvent(request.msg());
}

void Manager::NotifyARCVPNSocketConnectionEvent(
    const NotifyARCVPNSocketConnectionEventRequest& request) {
  if (!request.has_msg()) {
    LOG(ERROR) << __func__ << ": no message attached.";
    return;
  }
  counters_svc_.HandleARCVPNSocketConnectionEvent(request.msg());
}

bool Manager::SetFeatureFlag(
    patchpanel::SetFeatureFlagRequest::FeatureFlag flag, bool enabled) {
  bool old_flag = false;
  std::string_view feature_name = "Unknown";
  switch (flag) {
    case patchpanel::SetFeatureFlagRequest::FeatureFlag::
        SetFeatureFlagRequest_FeatureFlag_WIFI_QOS:
      feature_name = "WiFi QoS";
      old_flag = qos_svc_.is_enabled();
      if (enabled) {
        qos_svc_.Enable();
      } else {
        qos_svc_.Disable();
      }
      break;
    case patchpanel::SetFeatureFlagRequest::FeatureFlag::
        SetFeatureFlagRequest_FeatureFlag_CLAT:
      feature_name = "CLAT";
      old_flag = clat_svc_.is_enabled();
      if (enabled) {
        clat_svc_.Enable();
      } else {
        clat_svc_.Disable();
      }
      break;
    default:
      LOG(ERROR) << __func__ << "Unknown feature flag: " << flag;
      break;
  }

  LOG(INFO) << __func__ << ": set " << feature_name << " to " << enabled
            << " from " << old_flag;
  return old_flag;
}

}  // namespace patchpanel
