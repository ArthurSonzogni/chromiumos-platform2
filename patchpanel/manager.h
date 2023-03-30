// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MANAGER_H_
#define PATCHPANEL_MANAGER_H_

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/metrics_library.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/counters_service.h"
#include "patchpanel/crostini_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/file_descriptor_watcher_posix.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/network_monitor_service.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/subprocess_controller.h"
#include "patchpanel/system.h"

namespace shill {
class ProcessManager;
}  // namespace shill

namespace patchpanel {

// Struct to specify which forwarders to start and stop.
struct ForwardingSet {
  bool ipv6;
  bool multicast;
};

// Main class that runs the mainloop and responds to LAN interface changes.
class Manager final : public brillo::DBusDaemon {
 public:
  Manager(int argc, char* argv[]);
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

  ~Manager() = default;

  void StartForwarding(const std::string& ifname_physical,
                       const std::string& ifname_virtual,
                       const ForwardingSet& fs = {.ipv6 = true,
                                                  .multicast = true});

  void StopForwarding(const std::string& ifname_physical,
                      const std::string& ifname_virtual,
                      const ForwardingSet& fs = {.ipv6 = true,
                                                 .multicast = true});

  // This function is used to enable specific features only on selected
  // combination of Android version, Chrome version, and boards.
  // Empty |supportedBoards| means that the feature should be enabled on all
  // board.
  static bool ShouldEnableFeature(
      int min_android_sdk_version,
      int min_chrome_milestone,
      const std::vector<std::string>& supported_boards,
      const std::string& feature_name);

 protected:
  int OnInit() override;

 private:
  void OnShillDefaultLogicalDeviceChanged(
      const ShillClient::Device& new_device,
      const ShillClient::Device& prev_device);
  void OnShillDefaultPhysicalDeviceChanged(
      const ShillClient::Device& new_device,
      const ShillClient::Device& prev_device);
  void OnShillDevicesChanged(const std::vector<std::string>& added,
                             const std::vector<std::string>& removed);
  void OnIPConfigsChanged(const std::string& ifname,
                          const ShillClient::IPConfig& ipconfig);
  void OnIPv6NetworkChanged(const std::string& ifname,
                            const std::string& ipv6_address);

  void OnGuestDeviceChanged(const Device& virtual_device,
                            Device::ChangeEvent event,
                            GuestMessage::GuestType guest_type);

  void InitialSetup();

  bool StartArc(pid_t pid);
  void StopArc();
  bool StartArcVm(uint32_t cid);
  void StopArcVm(uint32_t cid);
  bool StartCrosVm(uint64_t vm_id,
                   GuestMessage::GuestType vm_type,
                   uint32_t subnet_index = kAnySubnetIndex);
  void StopCrosVm(uint64_t vm_id, GuestMessage::GuestType vm_type);

  // Callback from Daemon to notify that the message loop exits and before
  // Daemon::Run() returns.
  void OnShutdown(int* exit_code) override;

  // Handles DBus request for querying the list of virtual devices managed by
  // patchpanel.
  std::unique_ptr<dbus::Response> OnGetDevices(dbus::MethodCall* method_call);

  // Handles DBus notification indicating ARC++ is booting up.
  std::unique_ptr<dbus::Response> OnArcStartup(dbus::MethodCall* method_call);

  // Handles DBus notification indicating ARC++ is spinning down.
  std::unique_ptr<dbus::Response> OnArcShutdown(dbus::MethodCall* method_call);

  // Handles DBus notification indicating ARCVM is booting up.
  std::unique_ptr<dbus::Response> OnArcVmStartup(dbus::MethodCall* method_call);

  // Handles DBus notification indicating ARCVM is spinning down.
  std::unique_ptr<dbus::Response> OnArcVmShutdown(
      dbus::MethodCall* method_call);

  // Handles DBus notification indicating a Termina VM is booting up.
  std::unique_ptr<dbus::Response> OnTerminaVmStartup(
      dbus::MethodCall* method_call);

  // Handles DBus notification indicating a Termina VM is spinning down.
  std::unique_ptr<dbus::Response> OnTerminaVmShutdown(
      dbus::MethodCall* method_call);

  // Handles DBus notification indicating a Plugin VM is booting up.
  std::unique_ptr<dbus::Response> OnPluginVmStartup(
      dbus::MethodCall* method_call);

  // Handles DBus notification indicating a Plugin VM is spinning down.
  std::unique_ptr<dbus::Response> OnPluginVmShutdown(
      dbus::MethodCall* method_call);

  // Handles DBus requests for setting a VPN intent fwmark on a socket.
  std::unique_ptr<dbus::Response> OnSetVpnIntent(dbus::MethodCall* method_call);

  // Handles DBus requests for connect and routing an existing network
  // namespace created via minijail or through rtnetlink RTM_NEWNSID.
  std::unique_ptr<dbus::Response> OnConnectNamespace(
      dbus::MethodCall* method_call);

  // Handles DBus requests for querying traffic counters.
  std::unique_ptr<dbus::Response> OnGetTrafficCounters(
      dbus::MethodCall* method_call);

  // Handles DBus requests for creating iptables rules requests from
  // permission_broker.
  std::unique_ptr<dbus::Response> OnModifyPortRule(
      dbus::MethodCall* method_call);

  // Handles DBus requests for starting and stopping VPN lockdown.
  std::unique_ptr<dbus::Response> OnSetVpnLockdown(
      dbus::MethodCall* method_call);

  // Handles DBus requests for creating iptables rules requests from dns-proxy.
  std::unique_ptr<dbus::Response> OnSetDnsRedirectionRule(
      dbus::MethodCall* method_call);

  // Handles DBus requests for creating an L3 network on a network interface
  // and tethered to an upstream network.
  std::unique_ptr<dbus::Response> OnCreateTetheredNetwork(
      dbus::MethodCall* method_call);

  // Handles DBus requests for creating a local-only L3 network on a
  // network interface.
  std::unique_ptr<dbus::Response> OnCreateLocalOnlyNetwork(
      dbus::MethodCall* method_call);

  // Handles DBus requests for providing L3 and DHCP client information about
  // clients connected to a network created with OnCreateTetheredNetwork or
  // OnCreateLocalOnlyNetwork.
  std::unique_ptr<dbus::Response> OnDownstreamNetworkInfo(
      dbus::MethodCall* method_call);

  // Sends out DBus signal for notifying neighbor reachability event.
  void OnNeighborReachabilityEvent(
      int ifindex,
      const shill::IPAddress& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type);

  std::unique_ptr<patchpanel::ConnectNamespaceResponse> ConnectNamespace(
      base::ScopedFD client_fd,
      const patchpanel::ConnectNamespaceRequest& request);

  // Helper functions for tracking DBus request lifetime with file descriptors
  // provided by DBus clients. Consumes the file descriptor |dbus_fd| read from
  // DBus and returns a duplicate wrapped in base::ScopedFD. The duplicate is
  // added to the list of file descriptors watched for invalidation. Returns an
  // invalid ScopedFD object if it fails. The original fd is closed.
  base::ScopedFD AddLifelineFd(base::ScopedFD dbus_fd);
  bool DeleteLifelineFd(int dbus_fd);

  // Detects if any file descriptor committed in patchpanel's DBus API has been
  // invalidated by the caller. Calls OnLifelineFdClosed for any invalid fd
  // found.
  void OnLifelineFdClosed(int client_fd);

  bool RedirectDns(base::ScopedFD client_fd,
                   const patchpanel::SetDnsRedirectionRuleRequest& request);

  // Checks the validaty of a CreateTetheredNetwork or CreatedLocalOnlyNetwork
  // DBus request.
  bool ValidateDownstreamNetworkRequest(const DownstreamNetworkInfo& info);
  // Parse a DownstreamNetworkInfo object and a file descriptor from |reader|
  // using |parser| and creates a downstream L3 network for
  // CreateTetheredNetwork or CreatedLocalOnlyNetwork on the network interface
  // specified by the request. If successful, |client_fd| is monitored and
  // triggers the teardown of the network setup when closed.
  patchpanel::DownstreamNetworkResult OnDownstreamNetworkRequest(
      dbus::MessageReader* reader,
      bool (*parser)(dbus::MessageReader*, DownstreamNetworkInfo*));

  // Disable and re-enable IPv6 inside a namespace.
  void RestartIPv6(const std::string& netns_name);

  // Dispatch |msg| to child processes.
  void SendGuestMessage(const GuestMessage& msg);

  // Signal clients for network configuration change.
  void SendNetworkConfigurationChangedSignal();

  friend std::ostream& operator<<(std::ostream& stream, const Manager& manager);

  // Unique instance of patchpanel::System shared for all subsystems.
  std::unique_ptr<System> system_;
  // The singleton instance that manages the creation and exit notification of
  // each subprocess. All the subprocesses should be created by this.
  shill::ProcessManager* process_manager_;

  // UMA metrics client.
  std::unique_ptr<MetricsLibraryInterface> metrics_;
  // Shill Dbus client.
  std::unique_ptr<ShillClient> shill_client_;
  // High level routing and iptables controller service.
  std::unique_ptr<Datapath> datapath_;
  // Routing service.
  std::unique_ptr<RoutingService> routing_svc_;
  // ARC++/ARCVM service.
  std::unique_ptr<ArcService> arc_svc_;
  // Crostini and other VM service.
  std::unique_ptr<CrostiniService> cros_svc_;
  // Patchpanel DBus service.
  dbus::ExportedObject* dbus_svc_path_;  // Owned by |bus_|.
  // adb connection forwarder service.
  std::unique_ptr<SubprocessController> adb_proxy_;
  // IPv4 and IPv6 Multicast forwarder service.
  std::unique_ptr<SubprocessController> mcast_proxy_;
  // IPv6 neighbor discovery forwarder process handler.
  std::unique_ptr<SubprocessController> nd_proxy_;
  // IPv6 address provisioning / ndp forwarding service.
  std::unique_ptr<GuestIPv6Service> ipv6_svc_;
  // Traffic counter service.
  std::unique_ptr<CountersService> counters_svc_;
  // L2 neighbor monitor service.
  std::unique_ptr<NetworkMonitorService> network_monitor_svc_;
  // IPv4 prefix and address manager.
  AddressManager addr_mgr_;

  // |cached_feature_enabled| stores the cached result of if a feature should be
  // enabled.
  static std::map<const std::string, bool> cached_feature_enabled_;

  // All namespaces currently connected through patchpanel ConnectNamespace
  // API, keyed by file descriptors committed by clients when calling
  // ConnectNamespace.
  std::map<int, ConnectedNamespace> connected_namespaces_;
  int connected_namespaces_next_id_{0};

  // DNS proxy's IPv4 and IPv6 addresses keyed by its guest interface.
  std::map<std::string, std::string> dns_proxy_ipv4_addrs_;
  std::map<std::string, std::string> dns_proxy_ipv6_addrs_;

  // All external network interfaces currently managed by patchpanel through
  // the CreateTetheredNetwork or CreateLocalOnlyNetwork DBus APIs, keyed by the
  // file descriptors committed by the DBus clients.
  std::map<int, DownstreamNetworkInfo> downstream_networks_;

  // All rules currently created through patchpanel RedirectDns
  // API, keyed by file descriptors committed by clients when calling the
  // API.
  std::map<int, DnsRedirectionRule> dns_redirection_rules_;

  // For each fd (process) committed through a patchpanel's DBus API, keep
  // track of the FileDescriptorWatcher::Controller object associated with it.
  std::map<int, std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      lifeline_fd_controllers_;

  base::WeakPtrFactory<Manager> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MANAGER_H_
