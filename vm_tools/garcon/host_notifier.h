// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_GARCON_HOST_NOTIFIER_H_
#define VM_TOOLS_GARCON_HOST_NOTIFIER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path_watcher.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/container_host.grpc.pb.h>

#include "vm_tools/garcon/ansible_playbook_application.h"
#include "vm_tools/garcon/package_kit_proxy.h"

namespace vm_tools {
namespace garcon {

// Handles making calls to cicerone running in the host.
class HostNotifier : public PackageKitProxy::PackageKitObserver,
                     public AnsiblePlaybookApplicationObserver {
 public:
  // Creates and inits the HostNotifier for running on the current sequence.
  // Returns null if there was any failure.
  static std::unique_ptr<HostNotifier> Create(base::Closure shutdown_closure);

  // Sends a gRPC call to the host to notify it to open the specified URL with
  // the web browser. Returns true on success, false otherwise.
  static bool OpenUrlInHost(const std::string& url);

  // Sends a gRPC call to the host to notify it to open a terminal window that
  // is connected to this container. |args| will be executed as a program in
  // the terminal if any are passed.
  static bool OpenTerminal(std::vector<std::string> args);

  ~HostNotifier() override;

  // Notifies the host that garcon is ready. This will send the initial update
  // for the application list and also establish a watcher for any updates to
  // the list of installed applications. Returns false if there was any failure.
  bool Init(uint32_t vsock_port, PackageKitProxy* package_kit_proxy);

  // vm_tools::garcon::PackageKitObserver overrides.
  void OnInstallCompletion(const std::string& command_uuid,
                           bool success,
                           const std::string& failure_reason) override;
  void OnInstallProgress(
      const std::string& command_uuid,
      vm_tools::container::InstallLinuxPackageProgressInfo::Status status,
      uint32_t percent_progress) override;
  void OnUninstallCompletion(bool success,
                             const std::string& failure_reason) override;
  void OnUninstallProgress(uint32_t percent_progress) override;

  // vm_tools::garcon::AnsiblePlaybookApplicationObserver overrides.
  void OnApplyAnsiblePlaybookCompletion(
      bool success, const std::string& failure_reason) override;

 private:
  // Callback structure for SendAppListToHost callback chain.
  struct AppListBuilderState {
    // The protobuffer we will return to the caller.
    vm_tools::container::UpdateApplicationListRequest request;

    // The actual paths to the .desktop files we used to get the applications
    // in |request|. This must correspond 1-to-1 with the entries in
    // |request.application| (same number, same order).
    std::vector<base::FilePath> desktop_files_for_application;

    // Number of .desktop files we have already queried for their package_id.
    // Thus, also the index of the next .desktop file we need to query for
    // its package_id.
    int num_package_id_queries_completed = 0;
  };

  explicit HostNotifier(base::Closure shutdown_closure);

  // Sends a message to the host indicating that our server is ready for
  // accepting incoming calls.
  bool NotifyHostGarconIsReady(uint32_t vsock_port);

  // Sends a message to the host indicating the container is shutting down.
  void NotifyHostOfContainerShutdown();

  // Sends a message to the host indicating the number of triggered, but not yet
  // sent, app list updates.
  void NotifyHostOfPendingAppListUpdates();

  // Sends a list of the installed applications to the host.
  void SendAppListToHost();

  // Sends a list of the system configured MIME types to the host.
  void SendMimeTypesToHost();

  // Callback for PackageKitProxy::SearchLinuxPackagesForFile. Called each time
  // PackageKitProxy gets the package_id info for another .desktop file.
  void PackageIdCallback(std::unique_ptr<AppListBuilderState> state,
                         bool success,
                         bool pkg_found,
                         const PackageKitProxy::LinuxPackageInfo& pkg_info,
                         const std::string& error);

  // Callback for when desktop file path changes occur.
  void DesktopPathsChanged(const base::FilePath& path, bool error);

  // Callback for when changes to /etc/ or $HOME occur which hold the MIME types
  // files.
  void MimeTypesChanged(const base::FilePath& path, bool error);

  // Creates a ContainerListener::Stub, defaulting to vsock but falling back
  // to IPv4 if the host doesn't support vsock.
  void SetUpContainerListenerStub(const std::string& host_ip);

  // Kicks off the next step in the process of getting package_id data while
  // building an UpdateApplicationListRequest. It either kicks off another
  // request to PackageKit, or it finishes the request and sends it to the host.
  void RequestNextPackageIdOrCompleteUpdateApplicationList(
      std::unique_ptr<AppListBuilderState> state);

  // Called when signal_fd_ becomes readable.
  void OnSignalReadable();

  // gRPC stub for communicating with cicerone on the host.
  std::unique_ptr<vm_tools::container::ContainerListener::Stub> stub_;

  // Security token for communicating with cicerone.
  std::string token_;

  // Watchers for tracking filesystem changes to .desktop files/dirs,
  // /etc/mime.types and $HOME/.mime.types files.
  std::vector<std::unique_ptr<base::FilePathWatcher>> watchers_;

  // True if there is currently a delayed task pending for updating the
  // application list.
  bool update_app_list_posted_;

  // True if SendAppListToHost has started a callback chain which hasn't
  // completed yet.
  bool send_app_list_to_host_in_progress_;

  // True if there is currently a delayed task pending for updating the
  // MIME types list.
  bool update_mime_types_posted_;

  // Closure for stopping the MessageLoop.  Posted to the thread's TaskRunner
  // when this program receives a SIGTERM.
  base::Closure shutdown_closure_;

  // File descriptor for receiving signals.
  base::ScopedFD signal_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> signal_controller_;

  // Pointer to the PackageKit needed for querying package_id data.
  PackageKitProxy* package_kit_proxy_;  // Not owned.

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(HostNotifier);
};

}  // namespace garcon
}  // namespace vm_tools

#endif  // VM_TOOLS_GARCON_HOST_NOTIFIER_H_
