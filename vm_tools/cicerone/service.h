// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_SERVICE_H_
#define VM_TOOLS_CICERONE_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <brillo/process.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <grpcpp/grpcpp.h>
#include <vm_applications/proto_bindings/apps.pb.h>
#include <vm_cicerone/proto_bindings/cicerone_service.pb.h>
#include <vm_concierge/proto_bindings/concierge_service.pb.h>

#include "vm_tools/cicerone/container.h"
#include "vm_tools/cicerone/container_listener_impl.h"
#include "vm_tools/cicerone/crash_listener_impl.h"
#include "vm_tools/cicerone/shill_client.h"
#include "vm_tools/cicerone/tremplin_listener_impl.h"
#include "vm_tools/cicerone/virtual_machine.h"

namespace vm_tools {
namespace cicerone {

// VM Container Service responsible for responding to DBus method calls for
// interacting with VM containers.
class Service final {
 public:
  // Creates a new Service instance.  |quit_closure| is posted to the TaskRunner
  // for the current thread when this process receives a SIGTERM. |bus| is a
  // connection to the SYSTEM dbus.
  // Normally, services are bound to a AF_VSOCK and AF_UNIX socket. For unit
  // tests, the services only listen on an AF_UNIX socket by giving
  // |unix_socket_path_for_testing| a value.
  static std::unique_ptr<Service> Create(
      base::Closure quit_closure,
      const base::Optional<base::FilePath>& unix_socket_path_for_testing,
      scoped_refptr<dbus::Bus> bus);

  ~Service();

  ContainerListenerImpl* GetContainerListenerImpl() const {
    return container_listener_.get();
  }

  TremplinListenerImpl* GetTremplinListenerImpl() const {
    return tremplin_listener_.get();
  }

  CrashListenerImpl* GetCrashListenerImpl() const {
    return crash_listener_.get();
  }

  // For testing only. Pretend that the Tremplin server for the given VM is
  // actually at |tremplin_address| instead of the normal vsock address. Must
  // be called after the VM is created but before the corresponding
  // ConnectTremplin is called.
  bool SetTremplinStubOfVmForTesting(
      const std::string& owner_id,
      const std::string& vm_name,
      std::unique_ptr<vm_tools::tremplin::Tremplin::StubInterface>
          mock_tremplin_stub);

  // For testing only. Force the given VM to add a container with the indicated
  // security token. A VM with |owner_id|, |vm_name| must already exist. This is
  // the only way to get a consistent security token for unit tests & fuzz
  // tests. Returns true on success.
  bool CreateContainerWithTokenForTesting(const std::string& owner_id,
                                          const std::string& vm_name,
                                          const std::string& container_name,
                                          const std::string& container_token);

  // Stop Service from starting GRPC servers in a testing environment. Must
  // be called before calling Service::Init (and therefore Service::Create).
  static void DisableGrpcForTesting();

  // Connect to the Tremplin instance on the VM with the given |cid|.
  void ConnectTremplin(const uint32_t cid,
                       bool* result,
                       base::WaitableEvent* event);

  // The status of an ongoing LXD container create operation.
  enum class CreateStatus {
    UNKNOWN,
    CREATED,
    DOWNLOAD_TIMED_OUT,
    CANCELLED,
    FAILED,
  };

  // The status of an ongoing LXD container start operation.
  enum class StartStatus {
    UNKNOWN,
    STARTED,
    CANCELLED,
    FAILED,
  };

  // Notifies the service that a VM with |cid| has finished its create
  // operation of |container_name| with |status|. |failure_reason| will describe
  // the failure reason if status != CREATED. Sets |result| to true if the VM
  // cid is known. Signals |event| when done.
  void LxdContainerCreated(const uint32_t cid,
                           std::string container_name,
                           CreateStatus status,
                           std::string failure_reason,
                           bool* result,
                           base::WaitableEvent* event);

  // Notifies the service that a VM with |cid| is downloading a container
  // |container_name| with |download_progress| percentage complete. Sets
  // |result| to true if the VM cid is known. Signals |event| when done.
  void LxdContainerDownloading(const uint32_t cid,
                               std::string container_name,
                               int download_progress,
                               bool* result,
                               base::WaitableEvent* event);

  // Notifies the service that a VM with |cid| has finished its delete
  // operation of |container_name| with |status|. |failure_reason| will describe
  // the failure reason if status != DELETED. Sets |result| to true if the VM
  // cid is known. Signals |event| when done.
  void LxdContainerDeleted(
      const uint32_t cid,
      std::string container_name,
      vm_tools::tremplin::ContainerDeletionProgress::Status status,
      std::string failure_reason,
      bool* result,
      base::WaitableEvent* event);

  // Notifies the service that a VM with |cid| is starting a container
  // |container_name| with status |status|. |failure_reason| will describe the
  // failure reason if status == FAILED. Sets |result| to true if the VM cid
  // is known. Signals |event| when done.
  void LxdContainerStarting(const uint32_t cid,
                            std::string container_name,
                            StartStatus status,
                            std::string failure_reason,
                            bool* result,
                            base::WaitableEvent* event);

  // Notifies the service that a container with |container_token| and running
  // in a VM |cid| has completed startup. Sets |result| to true if this maps to
  // a currently running VM and |container_token| matches a security token for
  // that VM; false otherwise. Signals |event| when done.
  void ContainerStartupCompleted(const std::string& container_token,
                                 const uint32_t cid,
                                 const uint32_t garcon_vsock_port,
                                 bool* result,
                                 base::WaitableEvent* event);

  // Notifies the service that a container with |container_name| or
  // |container_token| and running in a VM with |cid| is shutting down. Sets
  // |result| to true if this maps to a currently running VM and
  // |container_token| matches a security token for that VM; false otherwise.
  // Signals |event| when done.  Callers from within the VM (tremplin) may set
  // |container_name|, but callers from wihtin a container (garcon) should not
  // be trusted to use |container_name| and must use |container_token|.
  void ContainerShutdown(std::string container_name,
                         std::string container_token,
                         const uint32_t cid,
                         bool* result,
                         base::WaitableEvent* event);

  // Notifies the service of a change in |listening_tcp4_ports| for the VM with
  // |cid|. Sets |result| to true if this maps to a currently running VM; false
  // otherwise. Signals |event| when done.
  void UpdateListeningPorts(
      std::map<std::string, std::vector<uint16_t>> listening_tcp4_ports,
      const uint32_t cid,
      bool* result,
      base::WaitableEvent* event);

  // Sends a D-Bus signal to inform listeners on update for the progress or
  // completion of container export. It will use |cid| to resolve the request to
  // a VM. |progress_signal| should have all related fields set |result| is set
  // to true on success, false otherwise. Signals |event| when done.
  void ContainerExportProgress(
      const uint32_t cid,
      ExportLxdContainerProgressSignal* progress_signal,
      bool* result,
      base::WaitableEvent* event);

  // Sends a D-Bus signal to inform listeners on update for the progress or
  // completion of container import. It will use |cid| to resolve the request to
  // a VM. |progress_signal| should have all related fields set |result| is set
  // to true on success, false otherwise. Signals |event| when done.
  void ContainerImportProgress(
      const uint32_t cid,
      ImportLxdContainerProgressSignal* progress_signal,
      bool* result,
      base::WaitableEvent* event);

  // Sends a D-Bus signal to inform listeners of progress or completion of a
  // container upgrade. It will use |cid| to resolve the request to
  // a VM. |progress_signal| should have all related fields set |result| is set
  // to true on success, false otherwise. Signals |event| when done.
  void ContainerUpgradeProgress(const uint32_t cid,
                                UpgradeContainerProgressSignal* progress_signal,
                                bool* result,
                                base::WaitableEvent* event);

  void PendingUpdateApplicationListCalls(const std::string& container_token,
                                         const uint32_t cid,
                                         const uint32_t count,
                                         bool* result,
                                         base::WaitableEvent* event);

  // This will send a D-Bus message to Chrome to inform it of the current
  // installed application list for a container. It will use |cid| to
  // resolve the request to a VM and then |container_token| to resolve it to a
  // container. |app_list| should be populated with the list of installed
  // applications but the vm & container names should be left blank; it must
  // remain valid for the lifetime of this call. |result| is set to true on
  // success, false otherwise. Signals |event| when done.
  void UpdateApplicationList(const std::string& container_token,
                             const uint32_t cid,
                             vm_tools::apps::ApplicationList* app_list,
                             bool* result,
                             base::WaitableEvent* event);

  // Sends a D-Bus message to Chrome to tell it to open the |url| in a new tab.
  // |result| is set to true on success, false otherwise. Signals
  // |event| when done.
  void OpenUrl(const std::string& container_token,
               const std::string& url,
               uint32_t cid,
               bool* result,
               base::WaitableEvent* event);

  // Sends a D-Bus signal to inform listeners on update for the progress or
  // completion of a Linux package install. It will use |cid| to
  // resolve the request to a VM and then |container_token| to resolve it to a
  // container. |progress_signal| should have all related fields from the
  // container request set in it. |result| is set to true on success, false
  // otherwise. Signals |event| when done.
  void InstallLinuxPackageProgress(
      const std::string& container_token,
      const uint32_t cid,
      InstallLinuxPackageProgressSignal* progress_signal,
      bool* result,
      base::WaitableEvent* event);

  // Sends a D-Bus signal to inform Chrome about the progress or completion of a
  // Linux package uninstall. It will use |cid| to resolve the request to a VM
  // and then |container_token| to resolve it to a container. |progress_signal|
  // should have all related fields from the container request set in it.
  // |result| is set to true on success, false otherwise. Signals |event| when
  // done.
  void UninstallPackageProgress(const std::string& container_token,
                                const uint32_t cid,
                                UninstallPackageProgressSignal* progress_signal,
                                bool* result,
                                base::WaitableEvent* event);

  // Sends a D-Bus signal to inform listeners on update for the progress or
  // completion of a Ansible playbook application. It will use |cid| to
  // resolve the request to a VM and then |container_token| to resolve it to a
  // container. |progress_signal| should have all related fields from the
  // container request set in it. |result| is set to true on success, false
  // otherwise. Signals |event| when done.
  void ApplyAnsiblePlaybookProgress(
      const std::string& container_token,
      const uint32_t cid,
      ApplyAnsiblePlaybookProgressSignal* progress_signal,
      bool* result,
      base::WaitableEvent* event);

  // Sends a D-Bus message to Chrome to tell it to open a terminal that is
  // connected back to the VM/container and if there are params in
  // |terminal_params| then those should be executed in that terminal.
  // It will use |cid| to resolve the request to a VM and then
  // |container_token| to resolve it to a container.  |result| is set to true on
  // success, false otherwise. Signals |event| when done.
  void OpenTerminal(const std::string& container_token,
                    vm_tools::apps::TerminalParams terminal_params,
                    const uint32_t cid,
                    bool* result,
                    base::WaitableEvent* event);

  // Sends a D-Bus message to Chrome to update the list of file extensions to
  // MIME type mapping in the container, the mappings are contained in
  // |mime_types|. It will use |cid| to resolve the request to a VM and then
  // |container_token| to resolve it to a container.  |result| is set to true on
  // success, false otherwise. Signals |event| when done.
  void UpdateMimeTypes(const std::string& container_token,
                       vm_tools::apps::MimeTypes mime_types,
                       const uint32_t cid,
                       bool* result,
                       base::WaitableEvent* event);

 private:
  explicit Service(base::Closure quit_closure, scoped_refptr<dbus::Bus> bus);

  // Initializes the service by exporting our DBus methods, taking ownership of
  // its name, and starting our gRPC servers. If |unix_socket_path_for_testing|
  // has a value, the services are bound only to an AF_UNIX socket in that
  // directory instead of the normal VSOCK and AF_UNIX socket.
  bool Init(const base::Optional<base::FilePath>& unix_socket_path_for_testing);

  // Handles the termination of a child process.
  void HandleChildExit();

  // Handles a SIGTERM.
  void HandleSigterm();

  // Handles notification a VM is starting.
  std::unique_ptr<dbus::Response> NotifyVmStarted(
      dbus::MethodCall* method_call);

  // Handles a notification a VM is stopping.
  std::unique_ptr<dbus::Response> NotifyVmStopped(
      dbus::MethodCall* method_call);

  // Handles a request to get a security token to associate with a container.
  std::unique_ptr<dbus::Response> GetContainerToken(
      dbus::MethodCall* method_call);

  // Handles a request to launch an application in a container.
  std::unique_ptr<dbus::Response> LaunchContainerApplication(
      dbus::MethodCall* method_call);

  // Handles a request to get application icons in a container.
  std::unique_ptr<dbus::Response> GetContainerAppIcon(
      dbus::MethodCall* method_call);

  // Handles a request to launch vshd in a container.
  std::unique_ptr<dbus::Response> LaunchVshd(dbus::MethodCall* method_call);

  // Handles a request to get Linux package info from a container.
  std::unique_ptr<dbus::Response> GetLinuxPackageInfo(
      dbus::MethodCall* method_call);

  // Handles a request to install a Linux package file in a container.
  std::unique_ptr<dbus::Response> InstallLinuxPackage(
      dbus::MethodCall* method_call);

  // Handles a request to uninstall the Linux package that owns the indicated
  // .desktop file.
  std::unique_ptr<dbus::Response> UninstallPackageOwningFile(
      dbus::MethodCall* method_call);

  // Handles a request to create an LXD container.
  std::unique_ptr<dbus::Response> CreateLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to delete an LXD container.
  std::unique_ptr<dbus::Response> DeleteLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to start an LXD container.
  std::unique_ptr<dbus::Response> StartLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to set the default timezone for an LXD instance.
  std::unique_ptr<dbus::Response> SetTimezone(dbus::MethodCall* method_call);

  // Handles a request to get the primary username for an LXD container.
  std::unique_ptr<dbus::Response> GetLxdContainerUsername(
      dbus::MethodCall* method_call);

  // Handles a request to set up the user for an LXD container.
  std::unique_ptr<dbus::Response> SetUpLxdContainerUser(
      dbus::MethodCall* method_call);

  // Handles a request to export an LXD container.
  std::unique_ptr<dbus::Response> ExportLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to cancel an ongoing LXD container export.
  std::unique_ptr<dbus::Response> CancelExportLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to import an LXD container.
  std::unique_ptr<dbus::Response> ImportLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to cancel an ongoing LXD container import.
  std::unique_ptr<dbus::Response> CancelImportLxdContainer(
      dbus::MethodCall* method_call);

  // Handles a request to connect to chunnel.
  std::unique_ptr<dbus::Response> ConnectChunnel(dbus::MethodCall* method_call);

  // Handles a request to get debug information.
  std::unique_ptr<dbus::Response> GetDebugInformation(
      dbus::MethodCall* method_call);

  // Handles a request to apply Ansible playbook to a container.
  std::unique_ptr<dbus::Response> ApplyAnsiblePlaybook(
      dbus::MethodCall* method_call);

  // Handles a request to allow sideloading Arc (android) apps from the
  // container.
  std::unique_ptr<dbus::Response> ConfigureForArcSideload(
      dbus::MethodCall* method_call);

  // Handles a request to upgrade a container.
  std::unique_ptr<dbus::Response> UpgradeContainer(
      dbus::MethodCall* method_call);

  // Handles a request to cancel an ongoing container upgrade.
  std::unique_ptr<dbus::Response> CancelUpgradeContainer(
      dbus::MethodCall* method_call);

  // Gets the VirtualMachine that corresponds to a container at |cid|
  // or the |vm_token| for the VM itself and sets |vm_out| to the
  // VirtualMachine, |owner_id_out| to the owner id of the VM, and |name_out| to
  // the name of the VM. Returns false if no such mapping exists.
  bool GetVirtualMachineForCidOrToken(const uint32_t cid,
                                      const std::string& vm_token,
                                      VirtualMachine** vm_out,
                                      std::string* owner_id_out,
                                      std::string* name_out);

  // Gets the container's SSH keys from concierge.
  bool GetContainerSshKeys(const std::string& owner_id,
                           const std::string& vm_name,
                           const std::string& container_name,
                           std::string* host_pubkey_out,
                           std::string* host_privkey_out,
                           std::string* container_pubkey_out,
                           std::string* container_privkey_out,
                           std::string* hostname_out,
                           std::string* error_out);

  // Registers |hostname| and |ip| with the hostname resolver service so that
  // the container is reachable from a known hostname.
  void RegisterHostname(const std::string& hostname, const std::string& ip);

  // Unregisters containers associated with this |vm| with |owner_id| and
  // |vm_name|.  All hostnames are removed from the hostname resolver service,
  // and the ContainerShutdown signal is sent via D-Bus.
  void UnregisterVmContainers(VirtualMachine* vm,
                              const std::string& owner_id,
                              const std::string& vm_name);

  // Unregisters |hostname| with the hostname resolver service.
  void UnregisterHostname(const std::string& hostname);

  // Callback for when the crosdns D-Bus service goes online (or is online
  // already) so we can then register the NameOwnerChanged callback.
  void OnCrosDnsServiceAvailable(bool service_is_available);

  // Callback for when the crosdns D-Bus service restarts so we can
  // re-register any of our hostnames that are active.
  void OnCrosDnsNameOwnerChanged(const std::string& old_owner,
                                 const std::string& new_owner);

  // Callback for when the localtime file is changed so that we can update
  // the timezone for containers.
  void OnLocaltimeFileChanged(const base::FilePath& path, bool error);

  void OnSignalReadable();

  // Handles default service changes from shill.
  void OnDefaultNetworkServiceChanged();

  // Send all listening ports to chunneld.
  void SendListeningPorts();

  // Gets a VirtualMachine pointer to the registered VM with corresponding
  // |owner_id| and |vm_name|. Returns a nullptr if not found.
  VirtualMachine* FindVm(const std::string& owner_id,
                         const std::string& vm_name);

  // File descriptor for SIGTERM/SIGCHLD event.
  base::ScopedFD signal_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  // The shill D-Bus client.
  std::unique_ptr<ShillClient> shill_client_;

  // Key for VMs in the map, which is the owner ID and VM name as a pair.
  using VmKey = std::pair<std::string, std::string>;
  // Running VMs.
  std::map<VmKey, std::unique_ptr<VirtualMachine>> vms_;

  // Map of TCP4 ports to forwarding targets.
  using TcpForwardTarget = std::pair<std::string, std::string>;
  std::map<uint16_t, TcpForwardTarget> listening_tcp4_ports_;

  // Connection to the system bus.
  scoped_refptr<dbus::Bus> bus_;
  dbus::ExportedObject* exported_object_;             // Owned by |bus_|.
  dbus::ObjectProxy* vm_applications_service_proxy_;  // Owned by |bus_|.
  dbus::ObjectProxy* url_handler_service_proxy_;      // Owned by |bus_|.
  dbus::ObjectProxy* chunneld_service_proxy_;         // Owned by |bus_|.
  dbus::ObjectProxy* crosdns_service_proxy_;          // Owned by |bus_|.
  dbus::ObjectProxy* concierge_service_proxy_;        // Owned by |bus_|.

  // The ContainerListener service.
  std::unique_ptr<ContainerListenerImpl> container_listener_;

  // Thread on which the ContainerListener service lives.
  base::Thread grpc_thread_container_{"gRPC Container Server Thread"};

  // The server where the ContainerListener service lives.
  std::shared_ptr<grpc::Server> grpc_server_container_;

  // The TremplinListener service.
  std::unique_ptr<TremplinListenerImpl> tremplin_listener_;

  // Thread on which the TremplinListener service lives.
  base::Thread grpc_thread_tremplin_{"gRPC Tremplin Server Thread"};

  // The server where the TremplinListener service lives.
  std::shared_ptr<grpc::Server> grpc_server_tremplin_;

  // The CrashListener service.
  std::unique_ptr<CrashListenerImpl> crash_listener_;

  // Thread on which the CrashListener service lives.
  base::Thread grpc_thread_crash_{"gRPC Crash Server Thread"};

  // The server where the CrashListener service lives.
  std::shared_ptr<grpc::Server> grpc_server_crash_;

  // Closure that's posted to the current thread's TaskRunner when the service
  // receives a SIGTERM.
  base::Closure quit_closure_;

  // Ensure calls are made on the right thread.
  base::SequenceChecker sequence_checker_;

  // Map of hostnames/IPs we have registered so we can re-register them if the
  // resolver service restarts.
  std::map<std::string, std::string> hostname_mappings_;

  // IP address registered for 'linuxhost' so we can swap this out on OpenUrl
  // calls.
  std::string linuxhost_ip_;

  // Owner of the primary VM, we only do hostname mappings for the primary VM.
  std::string primary_owner_id_;

  // Handle to the SSH port forwarding process.
  brillo::ProcessImpl ssh_process_;

  // Watcher to monitor changes to the system timezone file.
  base::FilePathWatcher localtime_watcher_;

  // Should Service start GRPC servers for ContainerListener and
  // TremplinListener Used for testing
  static bool run_grpc_;

  base::WeakPtrFactory<Service> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_SERVICE_H_
