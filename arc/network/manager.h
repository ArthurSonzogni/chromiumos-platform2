// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_MANAGER_H_
#define ARC_NETWORK_MANAGER_H_

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process_reaper.h>
#include <chromeos/dbus/service_constants.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "arc/network/address_manager.h"
#include "arc/network/arc_service.h"
#include "arc/network/crostini_service.h"
#include "arc/network/helper_process.h"
#include "arc/network/routing_service.h"
#include "arc/network/shill_client.h"
#include "arc/network/socket.h"
#include "arc/network/subnet.h"
#include "arc/network/traffic_forwarder.h"

namespace arc_networkd {

// Main class that runs the mainloop and responds to LAN interface changes.
class Manager final : public brillo::DBusDaemon, private TrafficForwarder {
 public:
  // Metadata for tracking state associated with a connected namespace.
  struct ConnectNamespaceInfo {
    // The pid of the client network namespace.
    pid_t pid;
    // Name of the shill device for routing outbound traffic from the client
    // namespace. Empty if outbound traffic should be forwarded to the highest
    // priority network (physical or virtual).
    std::string outbound_ifname;
    // Name of the "local" veth device visible on the host namespace.
    std::string host_ifname;
    // Name of the "remote" veth device moved into the client namespace.
    std::string client_ifname;
    // IPv4 subnet assigned to the client namespace.
    std::unique_ptr<Subnet> client_subnet;
  };

  Manager(std::unique_ptr<HelperProcess> adb_proxy,
          std::unique_ptr<HelperProcess> mcast_proxy,
          std::unique_ptr<HelperProcess> nd_proxy);
  ~Manager();

  // TrafficForwarder methods.

  void StartForwarding(const std::string& ifname_physical,
                       const std::string& ifname_virtual,
                       bool ipv6,
                       bool multicast) override;

  void StopForwarding(const std::string& ifname_physical,
                      const std::string& ifname_virtual,
                      bool ipv6,
                      bool multicast) override;

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
  void InitialSetup();

  bool StartArc(pid_t pid);
  void StopArc(pid_t pid);
  bool StartArcVm(uint32_t cid);
  void StopArcVm(uint32_t cid);
  bool StartCrosVm(uint64_t vm_id,
                   GuestMessage::GuestType vm_type,
                   uint32_t subnet_index = kAnySubnetIndex);
  void StopCrosVm(uint64_t vm_id, GuestMessage::GuestType vm_type);

  // Callback from ProcessReaper to notify Manager that one of the
  // subprocesses died.
  void OnSubprocessExited(pid_t pid, const siginfo_t& info);
  void RestartSubprocess(HelperProcess* subproc);

  // Callback from Daemon to notify that SIGTERM or SIGINT was received and
  // the daemon should clean up in preparation to exit.
  void OnShutdown(int* exit_code) override;

  // Callback from NDProxy telling us to add a new IPv6 route.
  void OnDeviceMessageFromNDProxy(const DeviceMessage& msg);

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
  // namespace created via minijail or through the rtnl
  std::unique_ptr<dbus::Response> OnConnectNamespace(
      dbus::MethodCall* method_call);

  void ConnectNamespace(base::ScopedFD client_fd,
                        const patchpanel::ConnectNamespaceRequest& request,
                        patchpanel::ConnectNamespaceResponse& response);
  void DisconnectNamespace(int client_fd);

  // Dispatch |msg| to child processes.
  void SendGuestMessage(const GuestMessage& msg);

  friend std::ostream& operator<<(std::ostream& stream, const Manager& manager);

  std::unique_ptr<ShillClient> shill_client_;
  std::unique_ptr<RoutingService> routing_svc_;

  // Guest services.
  std::unique_ptr<ArcService> arc_svc_;
  std::unique_ptr<CrostiniService> cros_svc_;

  // DBus service.
  dbus::ExportedObject* dbus_svc_path_;  // Owned by |bus_|.

  // Other services.
  brillo::ProcessReaper process_reaper_;
  std::unique_ptr<HelperProcess> adb_proxy_;
  std::unique_ptr<HelperProcess> mcast_proxy_;
  std::unique_ptr<HelperProcess> nd_proxy_;

  AddressManager addr_mgr_;

  // |cached_feature_enabled| stores the cached result of if a feature should be
  // enabled.
  static std::map<const std::string, bool> cached_feature_enabled_;

  std::unique_ptr<MinijailedProcessRunner> runner_;
  std::unique_ptr<Datapath> datapath_;

  // All namespaces currently connected through patchpanel ConnectNamespace
  // API, keyed by file descriptors committed by clients when calling
  // ConnectNamespace.
  std::map<int, ConnectNamespaceInfo> connected_namespaces_;
  int connected_namespaces_next_id_{0};

  base::WeakPtrFactory<Manager> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(Manager);
};

std::ostream& operator<<(std::ostream& stream,
                         const Manager::ConnectNamespaceInfo& ns_info);

}  // namespace arc_networkd

#endif  // ARC_NETWORK_MANAGER_H_
