// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_TERMINA_VM_H_
#define VM_TOOLS_CONCIERGE_TERMINA_VM_H_

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <chromeos/patchpanel/mac_address_generator.h>
#include <chromeos/patchpanel/subnet.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>
#include <vm_concierge/proto_bindings/concierge_service.pb.h>
#include <vm_protos/proto_bindings/vm_guest.grpc.pb.h>

#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/sigchld_handler.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

#include <brillo/grpc/async_grpc_client.h>

namespace vm_tools {
namespace concierge {

struct SigchldHelper;

struct VmFeatures {
  // Enable GPU in the started VM.
  bool gpu;

  // Provide software-based virtual Trusted Platform Module to the VM.
  bool software_tpm;

  // Enable audio capture function in the started VM.
  bool audio_capture;
};

// Represents a single instance of a running termina VM.
class TerminaVm final : public VmBaseImpl,
                        public std::enable_shared_from_this<TerminaVm> {
 public:
  // Type of a disk image.
  enum class DiskImageType {
    // Raw disk image file.
    RAW,

    // QCOW2 disk image.
    QCOW2,
  };

  // Describes a disk image to be mounted inside the VM.
  struct Disk {
    // Path to the disk image on the host.
    base::FilePath path;

    // Whether the disk should be writable by the VM.
    bool writable;

    // Whether the disk should allow sparse file operations (discard) by the VM.
    bool sparse;
  };

  enum class DiskResizeType {
    NONE,
    EXPAND,
    SHRINK,
  };

  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::shared_ptr<TerminaVm> Create(
      base::FilePath kernel,
      base::FilePath rootfs,
      int32_t cpus,
      std::vector<Disk> disks,
      uint32_t vsock_cid,
      std::unique_ptr<patchpanel::Client> network_client,
      std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
      base::FilePath runtime_dir,
      base::FilePath log_path,
      std::string rootfs_device,
      std::string stateful_device,
      uint64_t stateful_size,
      VmFeatures features,
      bool is_termina,
      std::weak_ptr<SigchldHandler> weak_async_sigchld_handler);
  ~TerminaVm() override;

  // Configures the network interfaces inside the VM.  Returns true iff
  // successful.
  bool ConfigureNetwork(const std::vector<std::string>& nameservers,
                        const std::vector<std::string>& search_domains);

  // Configures the VM to allow it to support a (single) container guest API
  // endpoint using |vm_token| as the container token.
  bool ConfigureContainerGuest(const std::string& vm_token,
                               std::string* out_error);

  // Mounts a file system inside the VM.  Both |source| (if it is a file path)
  // and |target| must be valid paths inside the VM.  Returns true on success.
  bool Mount(std::string source,
             std::string target,
             std::string fstype,
             uint64_t mountflags,
             std::string options);

  // Starts Termina-specific services in the guest.
  bool StartTermina(std::string lxd_subnet,
                    bool allow_privileged_containers,
                    std::string* out_error,
                    vm_tools::StartTerminaResponse* response);

  // Mount a 9p file system inside the VM.  The guest VM connects to a server
  // listening on the vsock port |port| and mounts the file system on |target|.
  bool Mount9P(uint32_t port, std::string target);

  // Mounts an extra disk device inside the VM an an external disk.  |source|
  // must be a valid path inside the VM.  |target| is a name of mount point
  // which will be created under /mnt/external inside the VM. Returns true on
  // success.
  bool MountExternalDisk(std::string source, std::string target_dir);

  // Sets the resolv.conf in the VM to |config|. Returns true if successful,
  // false if the resolv.conf in the guest could not be updated.
  bool SetResolvConfig(const std::vector<std::string>& nameservers,
                       const std::vector<std::string>& search_domains) override;

  // Reset IPv6 stack in the VM if needed. This is triggered during a default
  // network change. Return true if successful.
  void HostNetworkChanged() override;

  // Set the guest time to the current time as given by gettimeofday.
  bool SetTime(std::string* failure_reason) override;

  // The VM's cid.
  uint32_t cid() const { return vsock_cid_; }

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM.  Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const {
    if (seneschal_server_proxy_) {
      return seneschal_server_proxy_->handle();
    }

    return 0;
  }

  // The IPv4 address of the VM's gateway in network byte order.
  uint32_t GatewayAddress() const;

  // The IPv4 address of the VM in network byte order.
  uint32_t IPv4Address() const;

  // The netmask of the VM's subnet in network byte order.
  uint32_t Netmask() const;

  // The VM's container subnet netmask in network byte order. Returns INADDR_ANY
  // if there is no container subnet.
  uint32_t ContainerNetmask() const;

  // The VM's container subnet prefix length. Returns 0 if there is no container
  // subnet.
  size_t ContainerPrefixLength() const;

  // The first address in the VM's container subnet in network byte order.
  // Returns INADDR_ANY if there is no container subnet.
  uint32_t ContainerSubnet() const;

  // Name of the guest block device for the rootfs (e.g. /dev/vda, /dev/pmem0).
  std::string RootfsDevice() const { return rootfs_device_; }

  // Name of the guest block device for the stateful filesystem (e.g. /dev/vdb).
  std::string StatefulDevice() const { return stateful_device_; }

  // Whether a TremplinStartedSignal has been received for the VM.
  bool IsTremplinStarted() const { return is_tremplin_started_; }

  // VmInterface overrides.
  // Shuts down the VM.  First attempts a clean shutdown of the VM by sending
  // a Shutdown RPC to maitre'd.  If that fails, attempts to shut down the VM
  // using the control socket for the hypervisor.  If that fails, then sends a
  // SIGTERM to the hypervisor.  Finally, if nothing works forcibly stops the VM
  // by sending it a SIGKILL.  Returns true if the VM was shut down and false
  // otherwise.
  Future<bool> Shutdown() override;
  VmInterface::Info GetInfo() override;
  bool AttachUsbDevice(uint8_t bus,
                       uint8_t addr,
                       uint16_t vid,
                       uint16_t pid,
                       int fd,
                       UsbControlResponse* response) override;
  bool DetachUsbDevice(uint8_t port, UsbControlResponse* response) override;
  bool ListUsbDevice(std::vector<UsbDevice>* devices) override;
  bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) override;
  vm_tools::concierge::DiskImageStatus ResizeDisk(
      uint64_t new_size, std::string* failure_reason) override;
  vm_tools::concierge::DiskImageStatus GetDiskResizeStatus(
      std::string* failure_reason) override;
  uint64_t GetMinDiskSize() override;

  void SetTremplinStarted() override { is_tremplin_started_ = true; }
  void VmToolsStateChanged(bool running) override { NOTREACHED(); }

  // Adjusts the amount of CPU the Termina VM processes are allowed to use.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state);

  static std::shared_ptr<TerminaVm> CreateForTesting(
      std::unique_ptr<patchpanel::Subnet> subnet,
      uint32_t vsock_cid,
      base::FilePath runtime_dir,
      base::FilePath log_path,
      std::string rootfs_device,
      std::string stateful_device,
      uint64_t stateful_size,
      std::string kernel_version,
      std::unique_ptr<brillo::AsyncGrpcClient<vm_tools::Maitred>> client,
      std::unique_ptr<vm_tools::Maitred::Stub> stub,
      bool is_termina,
      std::weak_ptr<SigchldHandler> weak_async_sigchld_handler);

 private:
  TerminaVm(uint32_t vsock_cid,
            std::unique_ptr<patchpanel::Client> network_client,
            std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
            base::FilePath runtime_dir,
            base::FilePath log_path,
            std::string rootfs_device,
            std::string stateful_device,
            uint64_t stateful_size,
            VmFeatures features,
            bool is_termina,
            std::weak_ptr<SigchldHandler> weak_async_sigchld_handler);

  // Constructor for testing only.
  TerminaVm(std::unique_ptr<patchpanel::Subnet> subnet,
            uint32_t vsock_cid,
            std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
            base::FilePath runtime_dir,
            base::FilePath log_path,
            std::string rootfs_device,
            std::string stateful_device,
            uint64_t stateful_size,
            VmFeatures features,
            bool is_termina,
            std::weak_ptr<SigchldHandler> weak_async_sigchld_handler);
  void HandleSuspendImminent() override;
  void HandleSuspendDone() override;
  // Returns the path to the VM control socket.
  std::string GetVmSocketPath() const;

  // Returns the string value of the 'serial' arg passed to crosvm.
  // If |log_path_| is empty, syslog will be used.
  // |hardware| should be one of "serial" or "virtio-console".
  // |console_type| should be either "console" or "earlycon".
  std::string GetCrosVmSerial(std::string hardware,
                              std::string console_type) const;

  // Starts the VM with the given kernel and root file system.
  bool Start(base::FilePath kernel,
             base::FilePath rootfs,
             int32_t cpus,
             std::vector<Disk> disks);

  // Runs a crosvm subcommend.
  void RunCrosvmCommand(std::string command);

  // Helper version to record the VM kernel version at startup.
  void RecordKernelVersionForEnterpriseReporting();

  bool ResizeDiskImage(uint64_t new_size);
  bool ResizeFilesystem(uint64_t new_size);

  void DestroyAsyncClient();

  void set_kernel_version_for_testing(std::string kernel_version);
  void set_client_for_testing(
      std::unique_ptr<brillo::AsyncGrpcClient<vm_tools::Maitred>> client,
      std::unique_ptr<vm_tools::Maitred::Stub> stub);

  // Remove when C++17 is available
  std::weak_ptr<TerminaVm> weak_from_this() { return shared_from_this(); }

  // The /30 subnet assigned to the VM.
  std::unique_ptr<patchpanel::Subnet> subnet_;

  // An optional /28 container subnet.
  std::unique_ptr<patchpanel::Subnet> container_subnet_;

  // Virtual socket context id to be used when communicating with this VM.
  uint32_t vsock_cid_;

  // Termina network device.
  patchpanel::NetworkDevice network_device_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Flags passed to vmc start.
  VmFeatures features_;

  // Stub for making RPC requests to the maitre'd process inside the VM.
  std::unique_ptr<vm_tools::Maitred::Stub> stub_;
  std::unique_ptr<brillo::AsyncGrpcClient<vm_tools::Maitred>> client_;

  // Whether a TremplinStartedSignal has been received for the VM.
  bool is_tremplin_started_ = false;

  // Kernel version retrieved at startup.
  std::string kernel_version_;

  // Rootfs device name.
  std::string rootfs_device_;

  // Stateful device name.
  std::string stateful_device_;

  // Current size of the stateful disk.
  uint64_t stateful_size_;

  // Target size of the stateful disk during a resize (when
  // stateful_resize_type_ is not NONE).
  uint64_t stateful_target_size_;

  // Type of disk resize currently in progress.
  // If this is NONE, then no resize is in progress right now.
  enum DiskResizeType stateful_resize_type_;

  // Status of the current resize operation (or most recent resize operation,
  // if no resize is currently in progress).
  vm_tools::concierge::DiskImageStatus last_stateful_resize_status_ =
      DiskImageStatus::DISK_STATUS_RESIZED;

  base::FilePath log_path_;

  // Confusingly, this class is also used for non-termina VMs that don't fit in
  // other types. This bool indicates if the VM is really a termina VM.
  const bool is_termina_;

  // Register to the main thread's signal handler for async sigchld waiting
  std::weak_ptr<SigchldHandler> weak_async_sigchld_handler_;

  // Prevent calling Shutdown twice (manual shutdown and destructor)
  bool already_shut_down_ = false;

  DISALLOW_COPY_AND_ASSIGN(TerminaVm);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_TERMINA_VM_H_
