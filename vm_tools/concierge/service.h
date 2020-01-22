// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SERVICE_H_
#define VM_TOOLS_CONCIERGE_SERVICE_H_

#include <stdint.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arc/network/address_manager.h>
#include <arc/network/mac_address_generator.h>
#include <arc/network/subnet.h>
#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <grpcpp/grpcpp.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/disk_image.h"
#include "vm_tools/concierge/power_manager_client.h"
#include "vm_tools/concierge/shill_client.h"
#include "vm_tools/concierge/startup_listener_impl.h"
#include "vm_tools/concierge/termina_vm.h"
#include "vm_tools/concierge/untrusted_vm_utils.h"
#include "vm_tools/concierge/vm_interface.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

namespace vm_tools {

namespace concierge {

// VM Launcher Service responsible for responding to DBus method calls for
// starting, stopping, and otherwise managing VMs.
class Service final {
 public:
  // Creates a new Service instance.  |quit_closure| is posted to the TaskRunner
  // for the current thread when this process receives a SIGTERM.
  static std::unique_ptr<Service> Create(base::Closure quit_closure);
  ~Service();

 private:
  explicit Service(base::Closure quit_closure);

  // Initializes the service by connecting to the system DBus daemon, exporting
  // its methods, and taking ownership of it's name.
  bool Init();

  // Handles the termination of a child process.
  void HandleChildExit();

  // Handles a SIGTERM.
  void HandleSigterm();

  // Handles a request to start a VM.  |method_call| must have a StartVmRequest
  // protobuf serialized as an array of bytes.
  std::unique_ptr<dbus::Response> StartVm(dbus::MethodCall* method_call);

  // Handles a request to start a plugin-based VM.  |method_call| must have a
  // StartPluginVmRequest protobuf serialized as an array of bytes.
  std::unique_ptr<dbus::Response> StartPluginVm(dbus::MethodCall* method_call);

  // Handles a request to start ARCVM.  |method_call| must have a
  // StartArcVmRequest protobuf serialized as an array of bytes.
  std::unique_ptr<dbus::Response> StartArcVm(dbus::MethodCall* method_call);

  // Handles a request to stop a VM.  |method_call| must have a StopVmRequest
  // protobuf serialized as an array of bytes.
  std::unique_ptr<dbus::Response> StopVm(dbus::MethodCall* method_call);

  // Handles a request to stop all running VMs.
  std::unique_ptr<dbus::Response> StopAllVms(dbus::MethodCall* method_call);

  // Handles a request to get VM info.
  std::unique_ptr<dbus::Response> GetVmInfo(dbus::MethodCall* method_call);

  // Handles a request to get VM info specific to enterprise reporting.
  std::unique_ptr<dbus::Response> GetVmEnterpriseReportingInfo(
      dbus::MethodCall* method_call);

  // Handles a request to update all VMs' times to the current host time.
  std::unique_ptr<dbus::Response> SyncVmTimes(dbus::MethodCall* method_call);

  // Handles a request to create a disk image.
  std::unique_ptr<dbus::Response> CreateDiskImage(
      dbus::MethodCall* method_call);

  // Handles a request to destroy a disk image.
  std::unique_ptr<dbus::Response> DestroyDiskImage(
      dbus::MethodCall* method_call);

  // Handles a request to export a disk image.
  std::unique_ptr<dbus::Response> ExportDiskImage(
      dbus::MethodCall* method_call);

  // Handles a request to import a disk image.
  std::unique_ptr<dbus::Response> ImportDiskImage(
      dbus::MethodCall* method_call);

  // Handles a request to check status of a disk image operation.
  std::unique_ptr<dbus::Response> CheckDiskImageStatus(
      dbus::MethodCall* method_call);

  // Handles a request to cancel a disk image operation.
  std::unique_ptr<dbus::Response> CancelDiskImageOperation(
      dbus::MethodCall* method_call);

  // Run import/export disk image operation with given UUID.
  void RunDiskImageOperation(std::string uuid);

  // Handles a request to list existing disk images.
  std::unique_ptr<dbus::Response> ListVmDisks(dbus::MethodCall* method_call);

  // Handles a request to get the SSH keys for a container.
  std::unique_ptr<dbus::Response> GetContainerSshKeys(
      dbus::MethodCall* method_call);

  std::unique_ptr<dbus::Response> AttachUsbDevice(
      dbus::MethodCall* method_call);

  std::unique_ptr<dbus::Response> DetachUsbDevice(
      dbus::MethodCall* method_call);

  std::unique_ptr<dbus::Response> ListUsbDevices(dbus::MethodCall* method_call);

  std::unique_ptr<dbus::Response> GetDnsSettings(dbus::MethodCall* method_call);

  std::unique_ptr<dbus::Response> SetVmCpuRestriction(
      dbus::MethodCall* method_call);

  // Writes DnsConfigResponse protobuf into DBus message.
  void ComposeDnsResponse(dbus::MessageWriter* writer);

  // Handles DNS changes from shill.
  void OnResolvConfigChanged(std::vector<std::string> nameservers,
                             std::vector<std::string> search_domains);

  // Helper for starting termina VMs, e.g. starting lxd.
  bool StartTermina(TerminaVm* vm,
                    std::string* failure_reason,
                    vm_tools::StartTerminaResponse::MountResult* result);

  // Helpers for notifying cicerone and sending signals of VM started/stopped
  // events, and generating container tokens.
  void NotifyCiceroneOfVmStarted(const VmId& vm_id,
                                 uint32_t vsock_cid,
                                 std::string vm_token);
  void SendVmStartedSignal(const VmId& vm_id,
                           const vm_tools::concierge::VmInfo& vm_info,
                           vm_tools::concierge::VmStatus status);
  void NotifyVmStopped(const VmId& vm_id, int64_t cid);
  std::string GetContainerToken(const VmId& vm_id,
                                const std::string& container_name);

  void OnTremplinStartedSignal(dbus::Signal* signal);
  void OnVmToolsStateChangedSignal(dbus::Signal* signal);

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool is_connected);
  void OnSignalReadable();

  // Called by |power_manager_client_| when the device is about to suspend or
  // resumed from suspend.
  void HandleSuspendImminent();
  void HandleSuspendDone();

  using VmMap = std::map<VmId, std::unique_ptr<VmInterface>>;

  // Returns an iterator to vm with key (|owner_id|, |vm_name|). If no such
  // element exists, tries the former with |owner_id| equal to empty string.
  VmMap::iterator FindVm(const std::string& owner_id,
                         const std::string& vm_name);

  // Resource allocators for VMs.
  arc_networkd::MacAddressGenerator mac_address_generator_;
  arc_networkd::AddressManager network_address_manager_;
  VsockCidPool vsock_cid_pool_;

  // Current DNS resolution config.
  std::vector<std::string> nameservers_;
  std::vector<std::string> search_domains_;

  // File descriptor for the SIGCHLD events.
  base::ScopedFD signal_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  // Connection to the system bus.
  scoped_refptr<dbus::Bus> bus_;
  dbus::ExportedObject* exported_object_;       // Owned by |bus_|.
  dbus::ObjectProxy* cicerone_service_proxy_;   // Owned by |bus_|.
  dbus::ObjectProxy* seneschal_service_proxy_;  // Owned by |bus_|.
  dbus::ObjectProxy* vmplugin_service_proxy_;   // Owned by |bus_|.

  // The port number to assign to the next shared directory server.
  uint32_t next_seneschal_server_port_;

  // The subnet for plugin VMs.
  std::unique_ptr<arc_networkd::Subnet> plugin_subnet_;
  std::unique_ptr<arc_networkd::SubnetAddress> plugin_gateway_;

  // Active VMs keyed by VmId which is (owner_id, vm_name).
  VmMap vms_;

  // The shill D-Bus client.
  std::unique_ptr<ShillClient> shill_client_;

  // The power manager D-Bus client.
  std::unique_ptr<PowerManagerClient> power_manager_client_;

  // The StartupListener service.
  StartupListenerImpl startup_listener_;

  // Thread on which the StartupListener service lives.
  base::Thread grpc_thread_vm_{"gRPC VM Server Thread"};

  // The server where the StartupListener service lives.
  std::shared_ptr<grpc::Server> grpc_server_vm_;

  // Closure that's posted to the current thread's TaskRunner when the service
  // receives a SIGTERM.
  base::Closure quit_closure_;

  // Ensure calls are made on the right thread.
  base::SequenceChecker sequence_checker_;

  // Signal must be connected before we can call SetTremplinStarted in a VM.
  bool is_tremplin_started_signal_connected_ = false;

  // Indicates that the VMs are currently suspended and may not respond to RPCs.
  bool vms_suspended_ = false;

  // Indicates that we should update the resolv.conf file in each VM after
  // resume.  This can happen if we get a ResolvCongigChanged message from shill
  // before receiving a SuspendDone signal from powerd.  Attempting to update
  // the resolv.conf file will simply result in a timeout so we should do it
  // after a resume.
  bool update_resolv_config_on_resume_ = false;

  // Whether we should re-synchronize VM clocks on resume from sleep.
  const bool resync_vm_clocks_on_resume_;

  // List of currently executing operations to import/export disk images.
  struct DiskOpInfo {
    std::unique_ptr<DiskImageOperation> op;
    bool canceled;
    base::TimeTicks last_report_time;

    explicit DiskOpInfo(std::unique_ptr<DiskImageOperation> disk_op)
        : op(std::move(disk_op)),
          canceled(false),
          last_report_time(base::TimeTicks::Now()) {}
  };
  std::list<DiskOpInfo> disk_image_ops_;

  // The kernel version of the host.
  const KernelVersionAndMajorRevision host_kernel_version_;

  // Used to check for, and possibly enable, the conditions required for
  // untrusted VMs.
  std::unique_ptr<UntrustedVMUtils> untrusted_vm_utils_;

  base::WeakPtrFactory<Service> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SERVICE_H_
