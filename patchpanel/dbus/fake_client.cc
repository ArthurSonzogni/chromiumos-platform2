// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/fake_client.h"

#include <optional>

namespace patchpanel {

void FakeClient::RegisterOnAvailableCallback(
    base::RepeatingCallback<void(bool)> callback) {}

void FakeClient::RegisterProcessChangedCallback(
    base::RepeatingCallback<void(bool)> callback) {}

bool FakeClient::NotifyArcStartup(pid_t) {
  return true;
}

bool FakeClient::NotifyArcShutdown() {
  return true;
}

std::optional<Client::ArcVMAllocation> FakeClient::NotifyArcVmStartup(
    uint32_t cid) {
  return std::nullopt;
}

bool FakeClient::NotifyArcVmShutdown(uint32_t cid) {
  return true;
}

std::optional<Client::TerminaAllocation> FakeClient::NotifyTerminaVmStartup(
    uint32_t cid) {
  return std::nullopt;
}

bool FakeClient::NotifyTerminaVmShutdown(uint32_t cid) {
  return true;
}

std::optional<Client::ParallelsAllocation> FakeClient::NotifyParallelsVmStartup(
    uint64_t vm_id, int subnet_index) {
  return std::nullopt;
}

bool FakeClient::NotifyParallelsVmShutdown(uint64_t vm_id) {
  return true;
}

std::optional<Client::BruschettaAllocation>
FakeClient::NotifyBruschettaVmStartup(uint64_t vm_id) {
  return std::nullopt;
}

bool FakeClient::NotifyBruschettaVmShutdown(uint64_t vm_id) {
  return true;
}

std::optional<Client::BorealisAllocation> FakeClient::NotifyBorealisVmStartup(
    uint32_t vm_id) {
  return std::nullopt;
}

bool FakeClient::NotifyBorealisVmShutdown(uint32_t vm_id) {
  return true;
}

bool FakeClient::DefaultVpnRouting(const base::ScopedFD& socket) {
  return true;
}

bool FakeClient::RouteOnVpn(const base::ScopedFD& socket) {
  return true;
}

bool FakeClient::BypassVpn(const base::ScopedFD& socket) {
  return true;
}

std::pair<base::ScopedFD, Client::ConnectedNamespace>
FakeClient::ConnectNamespace(pid_t pid,
                             const std::string& outbound_ifname,
                             bool forward_user_traffic,
                             bool route_on_vpn,
                             Client::TrafficSource traffic_source,
                             bool static_ipv6) {
  return {};
}

void FakeClient::GetTrafficCounters(const std::set<std::string>& devices,
                                    GetTrafficCountersCallback callback) {
  if (devices.size() == 0) {
    std::move(callback).Run(
        {stored_traffic_counters_.begin(), stored_traffic_counters_.end()});
    return;
  }

  std::vector<Client::TrafficCounter> return_counters;
  for (const auto& counter : stored_traffic_counters_) {
    if (devices.find(counter.ifname) != devices.end())
      return_counters.push_back(counter);
  }

  std::move(callback).Run({return_counters.begin(), return_counters.end()});
}

bool FakeClient::ModifyPortRule(Client::FirewallRequestOperation op,
                                Client::FirewallRequestType type,
                                Client::FirewallRequestProtocol proto,
                                const std::string& input_ifname,
                                const std::string& input_dst_ip,
                                uint32_t input_dst_port,
                                const std::string& dst_ip,
                                uint32_t dst_port) {
  return true;
}

void FakeClient::SetVpnLockdown(bool enable) {}

base::ScopedFD FakeClient::RedirectDns(
    Client::DnsRedirectionRequestType type,
    const std::string& input_ifname,
    const std::string& proxy_address,
    const std::vector<std::string>& nameservers,
    const std::string& host_ifname) {
  return {};
}

std::vector<Client::VirtualDevice> FakeClient::GetDevices() {
  return {};
}

void FakeClient::RegisterVirtualDeviceEventHandler(
    VirtualDeviceEventHandler handler) {
  virtual_device_event_handlers_ = handler;
}

void FakeClient::RegisterNeighborReachabilityEventHandler(
    NeighborReachabilityEventHandler handler) {
  neighbor_event_handlers_.push_back(handler);
}

bool FakeClient::CreateTetheredNetwork(
    const std::string& downstream_ifname,
    const std::string& upstream_ifname,
    const std::optional<DHCPOptions>& dhcp_options,
    const std::optional<UplinkIPv6Configuration>& uplink_ipv6_config,
    const std::optional<int>& mtu,
    CreateTetheredNetworkCallback callback) {
  // TODO(b/239559602) Run synchronously or schedule |callback| to run if
  // necessary for unit tests.
  return true;
}

bool FakeClient::CreateLocalOnlyNetwork(
    const std::string& ifname, CreateLocalOnlyNetworkCallback callback) {
  // TODO(b/239559602) Run synchronously or schedule |callback| to run if
  // necessary for unit tests.
  return true;
}

bool FakeClient::GetDownstreamNetworkInfo(
    const std::string& ifname, GetDownstreamNetworkInfoCallback callback) {
  // TODO(b/239559602) Run synchronously or schedule |callback| to run if
  // necessary for unit tests.
  return true;
}

bool FakeClient::ConfigureNetwork(int interface_index,
                                  std::string_view interface_name,
                                  uint32_t area,
                                  const net_base::NetworkConfig& network_config,
                                  net_base::NetworkPriority priority,
                                  NetworkTechnology technology,
                                  ConfigureNetworkCallback callback) {
  // TODO(b/293997937): schedule |callback| for unit tests.
  return true;
}

bool FakeClient::SendSetFeatureFlagRequest(Client::FeatureFlag flag,
                                           bool enable) {
  return true;
}

void FakeClient::TriggerNeighborReachabilityEvent(
    const NeighborReachabilityEvent& signal) {
  for (const auto& handler : neighbor_event_handlers_)
    handler.Run(signal);
}

}  // namespace patchpanel
