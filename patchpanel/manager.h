// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MANAGER_H_
#define PATCHPANEL_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/net-base/process_manager.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/clat_service.h"
#include "patchpanel/counters_service.h"
#include "patchpanel/crostini_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/dbus_client_notifier.h"
#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/downstream_network_info.h"
#include "patchpanel/downstream_network_service.h"
#include "patchpanel/forwarding_service.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/lifeline_fd_service.h"
#include "patchpanel/multicast_counters_service.h"
#include "patchpanel/multicast_forwarder.h"
#include "patchpanel/multicast_metrics.h"
#include "patchpanel/network/network_applier.h"
#include "patchpanel/network_monitor_service.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/rtnl_client.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/subprocess_controller.h"
#include "patchpanel/system.h"

namespace patchpanel {

class QoSService;

// The core implementation of the patchpanel daemon.
class Manager : public ForwardingService {
 public:
  // The caller should guarantee |system|, |process_manager|, |metrics| and
  // |client| variables outlive the created Manager instance.
  Manager(const base::FilePath& cmd_path,
          System* system,
          net_base::ProcessManager* process_manager,
          MetricsLibraryInterface* metrics,
          DbusClientNotifier* dbus_client_notifier,
          std::unique_ptr<ShillClient> shill_client,
          std::unique_ptr<RTNLClient> rtnl_client);
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;
  virtual ~Manager();

  // Queries the list of virtual devices managed by patchpanel.
  GetDevicesResponse GetDevices() const;

  // Handles notification indicating ARC++ is booting up.
  bool ArcStartup(pid_t pid);

  // Handles notification indicating ARC++ is spinning down.
  void ArcShutdown();

  // Handles notification indicating ARCVM is booting up.
  std::optional<patchpanel::ArcVmStartupResponse> ArcVmStartup(uint32_t cid);

  // Handles notification indicating ARCVM is spinning down.
  void ArcVmShutdown(uint32_t cid);

  // Handles notification indicating a Termina VM is booting up.
  const CrostiniService::CrostiniDevice* const TerminaVmStartup(uint64_t vm_id);

  // Handles notification indicating a Termina VM is spinning down.
  void TerminaVmShutdown(uint64_t vm_id);

  // Handles notification indicating a Parallels VM is booting up.
  const CrostiniService::CrostiniDevice* const ParallelsVmStartup(
      uint64_t vm_id, uint32_t subnet_index);

  // Handles notification indicating a Parallels VM is spinning down.
  void ParallelsVmShutdown(uint64_t vm_id);

  // Handles notification indicating a Bruschetta VM is booting up.
  const CrostiniService::CrostiniDevice* const BruschettaVmStartup(
      uint64_t vm_id);

  // Handles notification indicating a Bruschetta VM is spinning down.
  void BruschettaVmShutdown(uint64_t vm_id);

  // Handles notification indicating a Borealis VM is booting up.
  const CrostiniService::CrostiniDevice* const BorealisVmStartup(
      uint64_t vm_id);

  // Handles notification indicating a Borealis VM is spinning down.
  void BorealisVmShutdown(uint64_t vm_id);

  // Connects and routes an existing network namespace created via minijail or
  // through rtnetlink RTM_NEWNSID.
  ConnectNamespaceResponse ConnectNamespace(
      const patchpanel::ConnectNamespaceRequest& request,
      base::ScopedFD client_fd);

  // Queries traffic counters.
  std::map<CountersService::CounterKey, CountersService::Counter>
  GetTrafficCounters(const std::set<std::string>& shill_devices) const;

  // Creates iptables rules requests from permission_broker.
  bool ModifyPortRule(const patchpanel::ModifyPortRuleRequest& request);

  // Starts or stops VPN lockdown.
  void SetVpnLockdown(bool enable_vpn_lockdown);

  // Creates iptables rules requests from dns-proxy.
  bool SetDnsRedirectionRule(
      const patchpanel::SetDnsRedirectionRuleRequest& request,
      base::ScopedFD client_fd);

  // Tags the socket pointed by |sock_fd| for routing and other purposes.
  bool TagSocket(const patchpanel::TagSocketRequest& request,
                 const base::ScopedFD& sock_fd);

  // Start/Stop forwarding multicast traffic to ARC when ARC power state
  // changes.
  // When power state changes into interactive, start forwarding IPv4 and IPv6
  // multicast mDNS and SSDP traffic for all non-WiFi interfaces, and for WiFi
  // interface only when Android WiFi multicast lock is held by any app in ARC.
  // When power state changes into non-interactive, stop forwarding multicast
  // traffic for all interfaces if enabled.
  void NotifyAndroidInteractiveState(bool is_interactive);

  // Start/Stop forwarding WiFi multicast traffic to and from ARC when Android
  // WiFi multicast lock held status changes. Start forwarding IPv4 and IPv6
  // multicast mDNS and SSDP traffic for WiFi interfaces only when
  // device power state is interactive and Android WiFi multicast lock is held
  // by any app in ARC, otherwise stop multicast forwarder for ARC WiFi
  // interface.
  void NotifyAndroidWifiMulticastLockChange(bool is_held);

  // Apply changes based on the socket connection event.
  // Currently this is only used for ARC socket connections and applies QoS
  // setup and resets the QoS setup on connection closed.
  void NotifySocketConnectionEvent(
      const NotifySocketConnectionEventRequest& request);

  // Apply changes based on the socket connection event from ARC VPN.
  // Currently this is only used for ARC VPN traffic counting.
  void NotifyARCVPNSocketConnectionEvent(
      const NotifyARCVPNSocketConnectionEventRequest& request);

  // Set feature enabled flag.
  bool SetFeatureFlag(patchpanel::SetFeatureFlagRequest::FeatureFlag flag,
                      bool enabled);

  void StartIPv6NDPForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      std::optional<int> mtu = std::nullopt,
      std::optional<int> hop_limit = std::nullopt) override;

  void StopIPv6NDPForwarding(const ShillClient::Device& shill_device,
                             std::string_view ifname_virtual) override;

  void StartBroadcastForwarding(const ShillClient::Device& shill_device,
                                std::string_view ifname_virtual) override;

  void StopBroadcastForwarding(const ShillClient::Device& shill_device,
                               std::string_view ifname_virtual) override;

  void StartMulticastForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      MulticastForwarder::Direction dir =
          MulticastForwarder::Direction::kTwoWays) override;

  void StopMulticastForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      MulticastForwarder::Direction dir =
          MulticastForwarder::Direction::kTwoWays) override;

  void ConfigureNetwork(int ifindex,
                        const std::string& ifname,
                        NetworkApplier::Area area,
                        const net_base::NetworkConfig& network_config,
                        net_base::NetworkPriority priority,
                        NetworkApplier::Technology technology);

  DownstreamNetworkService* downstream_network_service() {
    return downstream_network_svc_.get();
  }

 private:
  friend class ManagerTest;

  // The initialization tasks that are not necessary for handling dbus methods.
  void RunDelayedInitialization();

  // Callbacks from |shill_client_|.
  void OnShillDefaultLogicalDeviceChanged(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device);
  void OnShillDefaultPhysicalDeviceChanged(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device);
  void OnShillDevicesChanged(const std::vector<ShillClient::Device>& added,
                             const std::vector<ShillClient::Device>& removed);
  void OnIPConfigsChanged(const ShillClient::Device& shill_device);
  void OnIPv6NetworkChanged(const ShillClient::Device& shill_device);
  void OnDoHProvidersChanged(const ShillClient::DoHProviders& doh_providers);

  // Callback from |network_monitor_svc_|.
  void OnNeighborReachabilityEvent(
      int ifindex,
      const net_base::IPAddress& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type);

  // Tears down a ConnectedNamespace setup given its connected namespace id.
  void OnConnectedNamespaceAutoclose(int connected_namespace_id);
  // Tears down a DNS redirection rule request given the lifeline fd value
  // committed by the client dns-proxy instance.
  void OnDnsRedirectionRulesAutoclose(int lifeline_fd);

  const CrostiniService::CrostiniDevice* StartCrosVm(
      uint64_t vm_id,
      CrostiniService::VMType vm_type,
      uint32_t subnet_index = kAnySubnetIndex);
  void StopCrosVm(uint64_t vm_id, CrostiniService::VMType vm_type);

  std::vector<DownstreamClientInfo> GetDownstreamClientInfo(
      std::string_view downstream_ifname) const;

  // Disable and re-enable IPv6 inside a namespace.
  void RestartIPv6(std::string_view netns_name);

  // Dispatch |msg| to child processes.
  void SendGuestMessage(const GuestMessage& msg);

  // Owned by PatchpanelDaemon.
  System* system_;

  // UMA metrics client. Owned by PatchpanelDaemon.
  MetricsLibraryInterface* metrics_;

  // The client of the Manager.
  DbusClientNotifier* dbus_client_notifier_;

  // Shill Dbus client.
  std::unique_ptr<ShillClient> shill_client_;

  // rtnetlink client.
  std::unique_ptr<RTNLClient> rtnl_client_;

  // High level routing and iptables controller service.
  std::unique_ptr<Datapath> datapath_;
  // Routing service.
  std::unique_ptr<RoutingService> routing_svc_;
  // ARC++/ARCVM service.
  std::unique_ptr<ArcService> arc_svc_;
  // Crostini and other VM service.
  std::unique_ptr<CrostiniService> cros_svc_;

  // adb connection forwarder service.
  std::unique_ptr<SubprocessController> adb_proxy_;
  // IPv4 and IPv6 Multicast forwarder service.
  std::unique_ptr<SubprocessController> mcast_proxy_;
  // IPv6 neighbor discovery forwarder process handler.
  std::unique_ptr<SubprocessController> nd_proxy_;
  // Socket service process handler.
  std::unique_ptr<SubprocessController> socket_service_;

  // IPv6 address provisioning / ndp forwarding service.
  std::unique_ptr<GuestIPv6Service> ipv6_svc_;
  // CLAT service.
  std::unique_ptr<ClatService> clat_svc_;
  // Traffic counter service.
  std::unique_ptr<CountersService> counters_svc_;
  // Multicast packet counter service.
  std::unique_ptr<MulticastCountersService> multicast_counters_svc_;
  // L2 neighbor monitor service.
  std::unique_ptr<NetworkMonitorService> network_monitor_svc_;
  // QoS service.
  std::unique_ptr<QoSService> qos_svc_;
  // LifelineFD management service
  std::unique_ptr<LifelineFDService> lifeline_fd_svc_;
  // TetheredNetwork and LocalOnlyNetwork management service.
  std::unique_ptr<DownstreamNetworkService> downstream_network_svc_;

  // IPv4 prefix and address manager.
  AddressManager addr_mgr_;

  // All namespaces currently connected through patchpanel ConnectNamespace
  // API, keyed by the the namespace id of the ConnectedNamespace.
  std::map<int, ConnectedNamespace> connected_namespaces_;
  int connected_namespaces_next_id_{0};

  // DNS proxy's IPv4 and IPv6 addresses keyed by its guest interface.
  std::map<std::string, net_base::IPv4Address> dns_proxy_ipv4_addrs_;
  std::map<std::string, net_base::IPv6Address> dns_proxy_ipv6_addrs_;

  // All rules currently created through patchpanel RedirectDns
  // API, keyed by the host-side interface name of the ConnectedNamespace of the
  // target dns-proxy instance to which the queries should be redirected.
  std::map<int, DnsRedirectionRule> dns_redirection_rules_;

  // Fetches and reports multicast packet count to UMA metrics.
  std::unique_ptr<MulticastMetrics> multicast_metrics_;

  base::WeakPtrFactory<Manager> weak_factory_{this};
};

}  // namespace patchpanel
#endif  // PATCHPANEL_MANAGER_H_
