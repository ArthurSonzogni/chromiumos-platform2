// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_ARC_VM_H_
#define VM_TOOLS_CONCIERGE_ARC_VM_H_

#include <stdint.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/notreached.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <chromeos/patchpanel/mac_address_generator.h>
#include <libcrossystem/crossystem.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

namespace vm_tools {
namespace concierge {

// Disk index of the /data disk. It is the 4th disk in request.disks().
constexpr unsigned int kDataDiskIndex = 3;

// The CPU cgroup where all the ARCVM's main crosvm process and its vCPU threads
// should belong to.
constexpr char kArcvmVcpuCpuCgroup[] = "/sys/fs/cgroup/cpu/arcvm-vcpus";

// The CPU cgroup where all the ARCVM's crosvm processes (except for the
// `arcvm-vcpu` ones above) should belong to.
constexpr char kArcvmCpuCgroup[] = "/sys/fs/cgroup/cpu/arcvm";

// The value for setting the cgroup's CFS quota to unlimited.
constexpr int kCpuPercentUnlimited = -1;

struct ArcVmFeatures {
  // Whether the guest kernel root file system is writable.
  bool rootfs_writable;

  // Use development configuration directives in the started VM.
  bool use_dev_conf;

  // Apply the multi-arena config for jemalloc to low-RAM devices.
  bool low_mem_jemalloc_arenas_enabled;

  // Enable vmm swap.
  bool vmm_swap_enabled;
};

// Obtain virtiofs shared dir command-line parameter string for oem directory.
std::string GetOemEtcSharedDataParam(uid_t euid, gid_t egid);

// Represents a single instance of a running termina VM.
class ArcVm final : public VmBaseImpl {
 public:
  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::unique_ptr<ArcVm> Create(
      base::FilePath kernel,
      uint32_t vsock_cid,
      std::unique_ptr<patchpanel::Client> network_client,
      std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
      base::FilePath runtime_dir,
      base::FilePath data_disk_path,
      VmMemoryId vm_memory_id,
      ArcVmFeatures features,
      VmBuilder vm_builder);
  ~ArcVm() override;

  // The VM's cid.
  uint32_t cid() const { return vsock_cid_; }

  // ArcVmFeatures settings.
  bool rootfs_writable() const { return features_.rootfs_writable; }
  bool use_dev_conf() const { return features_.use_dev_conf; }

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM.  Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const {
    return seneschal_server_proxy_ ? seneschal_server_proxy_->handle() : 0;
  }

  // The IPv4 address of the VM in network byte order.
  uint32_t IPv4Address() const;

  // VmInterface overrides.
  bool Shutdown() override;
  VmInterface::Info GetInfo() override;
  // Currently only implemented for termina, returns "Not implemented".
  bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) override;
  bool AttachUsbDevice(uint8_t bus,
                       uint8_t addr,
                       uint16_t vid,
                       uint16_t pid,
                       int fd,
                       uint8_t* out_port) override;
  bool DetachUsbDevice(uint8_t port) override;
  const std::unique_ptr<BalloonPolicyInterface>& GetBalloonPolicy(
      const MemoryMargins& margins, const std::string& vm) override;
  bool ListUsbDevice(std::vector<UsbDeviceEntry>* devices) override;
  bool UsesExternalSuspendSignals() override { return true; }
  bool SetResolvConfig(
      const std::vector<std::string>& nameservers,
      const std::vector<std::string>& search_domains) override {
    return true;
  }
  // TODO(b/136143058): Implement SetTime calls.
  bool SetTime(std::string* failure_reason) override { return true; }
  // This VM does not use maitred to set timezone.
  bool SetTimezone(const std::string& timezone,
                   std::string* out_error) override {
    *out_error = "";
    return true;
  };
  void SetTremplinStarted() override { NOTREACHED(); }
  void VmToolsStateChanged(bool running) override { NOTREACHED(); }
  vm_tools::concierge::DiskImageStatus ResizeDisk(
      uint64_t new_size, std::string* failure_reason) override;
  vm_tools::concierge::DiskImageStatus GetDiskResizeStatus(
      std::string* failure_reason) override;

  // Returns the kernel parameters for the VM
  static std::vector<std::string> GetKernelParams(
      const crossystem::Crossystem& cros_system,
      const StartArcVmRequest& request,
      int seneschal_server_port);

  // Adjusts the amount of CPU the ARCVM processes are allowed to use. When
  // the state is CPU_RESTRICTION_BACKGROUND_WITH_CFS_QUOTA_ENFORCED, the
  // cpu.cfs_quota_us cgroup for ARCVM is updated with the |quota| value.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                  int quota);

 private:
  ArcVm(int32_t vsock_cid,
        std::unique_ptr<patchpanel::Client> network_client,
        std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
        base::FilePath runtime_dir,
        base::FilePath data_disk_path,
        VmMemoryId vm_memory_id,
        ArcVmFeatures features);
  ArcVm(const ArcVm&) = delete;
  ArcVm& operator=(const ArcVm&) = delete;

  void HandleSuspendImminent() override;
  void HandleSuspendDone() override;

  // Starts the VM with the given kernel and root file system.
  bool Start(base::FilePath kernel, VmBuilder vm_builder);

  // Selects which balloon policy to use, and tries to initialize it, which may
  // fail.
  void InitializeBalloonPolicy(const MemoryMargins& margins,
                               const std::string& vm);

  // Listens for LMKD connections to the Vsock
  bool SetupLmkdVsock();
  void HandleLmkdVsockAccept();
  void HandleLmkdVsockRead();

  std::vector<patchpanel::Client::VirtualDevice> network_devices_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Path to the virtio-blk disk image for /data.
  // An empty path is set if /data is not backed by virtio-blk.
  const base::FilePath data_disk_path_;

  // Flags passed to vmc start.
  ArcVmFeatures features_;

  // It may take a few tries to initialize a LimitCacheBalloonPolicy, but give
  // up and log an error after too many failures.
  int balloon_init_attempts_ = 30;

  // TODO(cwd): When we are sure what synchronization is needed to make sure the
  // host knows the correct zone sizes (which change during boot), then replace
  // this timeout.
  std::optional<base::Time> balloon_refresh_time_ = std::nullopt;

  // Max size of a LMKD packet received over the Vsock
  static constexpr size_t kLmkdPacketMaxSize = 8 * sizeof(int);
  static constexpr size_t kLmkdKillDecisionRequestPacketSize = 4 * sizeof(int);
  static constexpr size_t kLmkdKillDecisionReplyPacketSize = 3 * sizeof(int);

  // Must be kept in sync with lmk_host_cmd::LMK_PROCKILL_CANDIDATE defined in
  // arc_lmkd_hooks.h in Android
  static constexpr int32_t kLmkProcKillCandidate = 0;

  base::ScopedFD arcvm_lmkd_vsock_fd_;
  base::ScopedFD lmkd_client_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      lmkd_vsock_accept_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      lmkd_vsock_read_watcher_;

  // Ensure calls are made on the right thread.
  base::SequenceChecker sequence_checker_;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_ARC_VM_H_
