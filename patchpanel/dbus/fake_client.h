// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DBUS_FAKE_CLIENT_H_
#define PATCHPANEL_DBUS_FAKE_CLIENT_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "patchpanel/dbus/client.h"

namespace patchpanel {

// Fake implementation of patchpanel::ClientInterface which can be used in
// tests.
class BRILLO_EXPORT FakeClient : public Client {
 public:
  FakeClient() = default;
  ~FakeClient() = default;

  // Client overrides.
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

  std::optional<Client::ParallelsAllocation> NotifyParallelsVmStartup(
      uint64_t vm_id, int subnet_index) override;
  bool NotifyParallelsVmShutdown(uint64_t vm_id) override;

  std::optional<Client::BruschettaAllocation> NotifyBruschettaVmStartup(
      uint64_t vm_id) override;
  bool NotifyBruschettaVmShutdown(uint64_t vm_id) override;

  std::optional<Client::BorealisAllocation> NotifyBorealisVmStartup(
      uint32_t vm_id) override;
  bool NotifyBorealisVmShutdown(uint32_t vm_id) override;

  bool DefaultVpnRouting(const base::ScopedFD& socket) override;

  bool RouteOnVpn(const base::ScopedFD& socket) override;

  bool BypassVpn(const base::ScopedFD& socket) override;

  std::pair<base::ScopedFD, Client::ConnectedNamespace> ConnectNamespace(
      pid_t pid,
      const std::string& outbound_ifname,
      bool forward_user_traffic,
      bool route_on_vpn,
      Client::TrafficSource traffic_source,
      bool static_ipv6) override;

  void GetTrafficCounters(const std::set<std::string>& devices,
                          Client::GetTrafficCountersCallback callback) override;

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
      Client::NeighborReachabilityEventHandler handler) override;

  bool CreateTetheredNetwork(
      const std::string& downstream_ifname,
      const std::string& upstream_ifname,
      const std::optional<DHCPOptions>& dhcp_options,
      const std::optional<UplinkIPv6Configuration>& uplink_ipv6_config,
      const std::optional<int>& mtu,
      Client::CreateTetheredNetworkCallback callback) override;

  bool CreateLocalOnlyNetwork(
      const std::string& ifname,
      Client::CreateLocalOnlyNetworkCallback callback) override;

  bool GetDownstreamNetworkInfo(
      const std::string& ifname,
      Client::GetDownstreamNetworkInfoCallback callback) override;

  bool ConfigureNetwork(int interface_index,
                        std::string_view interface_name,
                        uint32_t area,
                        const net_base::NetworkConfig& network_config,
                        net_base::NetworkPriority priority,
                        NetworkTechnology technology,
                        ConfigureNetworkCallback callback) override;

  bool SendSetFeatureFlagRequest(Client::FeatureFlag flag,
                                 bool enable) override;

  // Triggers registered handlers for NeighborReachabilityEvent.
  void TriggerNeighborReachabilityEvent(
      const Client::NeighborReachabilityEvent& signal);

  void set_stored_traffic_counters(
      const std::vector<Client::TrafficCounter>& counters) {
    stored_traffic_counters_ = counters;
  }

 private:
  std::vector<Client::TrafficCounter> stored_traffic_counters_;
  std::vector<Client::NeighborReachabilityEventHandler>
      neighbor_event_handlers_;
  VirtualDeviceEventHandler virtual_device_event_handlers_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DBUS_FAKE_CLIENT_H_
