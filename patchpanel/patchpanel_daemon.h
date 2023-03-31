// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_PATCHPANEL_DAEMON_H_
#define PATCHPANEL_PATCHPANEL_DAEMON_H_

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/metrics_library.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/manager.h"
#include "patchpanel/system.h"

namespace shill {
class ProcessManager;
}  // namespace shill

namespace patchpanel {

// Main class that runs the main loop and responds to DBus RPC requests.
class PatchpanelDaemon final : public brillo::DBusDaemon,
                               public Manager::ClientNotifier {
 public:
  explicit PatchpanelDaemon(const base::FilePath& cmd_path);
  PatchpanelDaemon(const PatchpanelDaemon&) = delete;
  PatchpanelDaemon& operator=(const PatchpanelDaemon&) = delete;

  ~PatchpanelDaemon() = default;

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
  // Implements brillo::DBusDaemon.
  int OnInit() override;
  // Callback from Daemon to notify that the message loop exits and before
  // Daemon::Run() returns.
  void OnShutdown(int* exit_code) override;

  // Implements Manager::ClientNotifier.
  void OnNetworkDeviceChanged(const Device& virtual_device,
                              Device::ChangeEvent event) override;
  void OnNetworkConfigurationChanged() override;
  void OnNeighborReachabilityEvent(
      int ifindex,
      const shill::IPAddress& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type) override;

 private:
  void InitialSetup();

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

  friend std::ostream& operator<<(std::ostream& stream,
                                  const PatchpanelDaemon& daemon);

  // |cached_feature_enabled| stores the cached result of if a feature should be
  // enabled.
  static std::map<const std::string, bool> cached_feature_enabled_;

  // The file path of the patchpanel daemon binary.
  base::FilePath cmd_path_;

  // Unique instance of patchpanel::System shared for all subsystems.
  std::unique_ptr<System> system_;
  // The singleton instance that manages the creation and exit notification of
  // each subprocess. All the subprocesses should be created by this.
  shill::ProcessManager* process_manager_;
  // UMA metrics client.
  std::unique_ptr<MetricsLibraryInterface> metrics_;

  // Patchpanel DBus service.
  dbus::ExportedObject* dbus_svc_path_;  // Owned by |bus_|.

  std::unique_ptr<Manager> manager_;

  base::WeakPtrFactory<PatchpanelDaemon> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_PATCHPANEL_DAEMON_H_
