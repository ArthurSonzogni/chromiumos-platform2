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
#include <base/functional/callback_forward.h>
#include <base/notreached.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <libcrossystem/crossystem.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/crosvm_control.h"
#include "vm_tools/concierge/mm/balloon_metrics.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/virtio_blk_metrics.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vmm_swap_low_disk_policy.h"
#include "vm_tools/concierge/vmm_swap_metrics.h"
#include "vm_tools/concierge/vmm_swap_tbw_policy.h"
#include "vm_tools/concierge/vmm_swap_usage_policy.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

namespace vm_tools::concierge {

class ArcNetwork;

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

  // Whether LMKD in ARCVM should initialize the client and connect to the
  // VmMemoryManagementService
  bool use_vm_memory_management_client;
};

// Obtain virtiofs shared dir command-line parameter string for oem directory.
SharedDataParam GetOemEtcSharedDataParam(uid_t euid, gid_t egid);

// Represents a single instance of a running termina VM.
class ArcVm final : public VmBaseImpl {
 public:
  struct Config {
    base::FilePath kernel;
    uint32_t vsock_cid;
    std::unique_ptr<ArcNetwork> network;
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy;
    bool is_vmm_swap_enabled;
    // The metrics sender for vmm-swap feature.
    std::unique_ptr<VmmSwapMetrics> vmm_swap_metrics;
    std::unique_ptr<VmmSwapLowDiskPolicy> vmm_swap_low_disk_policy;
    const raw_ref<VmmSwapTbwPolicy> vmm_swap_tbw_policy;
    base::FilePath vmm_swap_usage_path;
    // The callback for notify other dbus service when vm is swapping.
    base::RepeatingCallback<void(SwappingState)> vm_swapping_notify_callback;
    // The metrics sender for the virtio-blk performance.
    std::unique_ptr<VirtioBlkMetrics> virtio_blk_metrics;
    // The metrics sender for virtio-balloon.
    std::unique_ptr<mm::BalloonMetrics> balloon_metrics;
    // `guest_memory_size` is the size of the guest memory in bytes which is
    // specified in VmBuilder.
    int64_t guest_memory_size;
    base::FilePath runtime_dir;
    base::FilePath data_disk_path;
    ArcVmFeatures features;
    std::unique_ptr<base::OneShotTimer> swap_policy_timer{
        std::make_unique<base::OneShotTimer>()};
    std::unique_ptr<base::RepeatingTimer> swap_state_monitor_timer{
        std::make_unique<base::RepeatingTimer>()};
    VmBuilder vm_builder;
  };

  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::unique_ptr<ArcVm> Create(Config config);
  ~ArcVm() override;

  // TODO(b/256052459): ArcVmTest access the constructor of ArcVm directly
  // because SetupLmkdVsock() and Start() which are called from ArcVm::Create()
  // don't have tests. Add tests for them and use ArcVm::Create() directly for
  // tests.
  friend class ArcVmTest;

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

  // VmBaseImpl overrides.
  bool Shutdown() override;
  VmBaseImpl::Info GetInfo() const override;
  // Currently only implemented for termina, returns "Not implemented".
  bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) override;
  bool AttachNetDevice(const std::string& tap_name, uint8_t* out_bus) override;
  bool DetachNetDevice(uint8_t bus) override;
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

  void HandleSwapVmRequest(const SwapVmRequest& request,
                           SwapVmCallback callback) override;
  void InflateAggressiveBalloon(AggressiveBalloonCallback callback) override;
  void StopAggressiveBalloon(AggressiveBalloonResponse& response) override;
  // Public for testing purpose.
  uint64_t DeflateBalloonOnLmkd(int oom_score_adj, uint64_t proc_size);

  void HandleStatefulUpdate(
      const spaced::StatefulDiskSpaceUpdate update) override;

  // Does clean up when the guest boot is done and the userland is ready.
  void HandleUserlandReady();

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
  explicit ArcVm(Config config);

  ArcVm(const ArcVm&) = delete;
  ArcVm& operator=(const ArcVm&) = delete;

  using VmmSwapStateChangeCallback =
      base::OnceCallback<void(SwapState new_state)>;

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

  // Handlers for aggressive balloon
  void StartAggressiveBalloonInflation(std::optional<BalloonStats> stats_opt);
  void DoAggressiveBalloonStep();
  void OnAggressiveBalloonStepDone(bool success);

  base::TimeDelta CalculateVmmSwapDurationTarget() const;
  void HandleSwapVmEnableRequest(SwapVmCallback callback);
  void HandleSwapVmForceEnableRequest(SwapVmResponse& response);
  void HandleSwapVmDisableRequest(SwapVmResponse& response);
  bool DisableVmmSwap(VmmSwapDisableReason reason, bool slow_file_cleanup);
  void OnVmmSwapLowDiskPolicyResult(bool can_enable);
  void ApplyVmmSwapPolicyResult(SwapVmCallback callback,
                                VmmSwapPolicyResult policy_result);
  void TrimVmmSwapMemory();
  void StartVmmSwapOut();
  void RunVmmSwapOutAfterTrim();
  base::expected<SwapStatus, std::string> FetchVmmSwapStatus();

  const patchpanel::Client::ArcVMAllocation& GetNetworkAllocation() const;

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

  // When aggressively inflates the balloon, it should stop when LMKD tries to
  // kill perceptible processes. The incremental diff should be 10 MiB since
  // perceptible processes usually have 30 ~ 100 MiB size.
  static constexpr int32_t kAggressiveBalloonIncrementSize = MiB(10);

  // Amount the aggressive balloon deflates by on a puff failure. This should
  // be enough to ensure memory allocations from guest userspace start
  // succeeding again, but the precise value is arbitrary.
  static constexpr uint64_t kAggressiveBalloonBackoffBytes =
      50 * kAggressiveBalloonIncrementSize;

  // See comment on aggressive_balloon_interval_.
  static constexpr base::TimeDelta kInitAggressiveBalloonInterval =
      base::Milliseconds(50);
  static constexpr base::TimeDelta kMaxAggressiveBalloonInterval =
      base::Milliseconds(200);
  static constexpr base::TimeDelta kAggressiveBalloonPuffTimeout =
      base::Seconds(1);

  base::ScopedFD arcvm_lmkd_vsock_fd_;
  base::ScopedFD lmkd_client_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      lmkd_vsock_accept_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      lmkd_vsock_read_watcher_;

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);

  bool is_vmm_swap_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool requested_slow_file_cleanup_ GUARDED_BY_CONTEXT(sequence_checker_) =
      false;
  base::Time last_vmm_swap_out_at_ GUARDED_BY_CONTEXT(sequence_checker_);
  // Metrics reporter for vmm-swap feature.
  std::unique_ptr<VmmSwapMetrics> vmm_swap_metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // Timer used to run vmm-swap policy. All operations for vmm-swap policy runs
  // on the main thread.
  std::unique_ptr<base::OneShotTimer> swap_policy_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::RepeatingTimer> swap_state_monitor_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<VmmSwapLowDiskPolicy> vmm_swap_low_disk_policy_
      GUARDED_BY_CONTEXT(sequence_checker_);
  const raw_ref<VmmSwapTbwPolicy> vmm_swap_tbw_policy_
      GUARDED_BY_CONTEXT(sequence_checker_);
  VmmSwapUsagePolicy vmm_swap_usage_policy_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SwapVmCallback pending_swap_vm_callback_
      GUARDED_BY_CONTEXT(sequence_checker_);
  base::RepeatingCallback<void(SwappingState)> vm_swapping_notify_callback_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool skip_swap_policy_ = false;
  const int64_t guest_memory_size_;

  uint64_t aggressive_balloon_target_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  AggressiveBalloonCallback aggressive_balloon_callback_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool aggressive_balloon_running_ GUARDED_BY_CONTEXT(sequence_checker_) =
      false;
  // Interval used between puffs of the aggressive balloon. This helps to
  // ensure that LMKD has enough time to react before the guest goes into an
  // OOM state and locks up. Puff timeouts trigger a deflate followed by a
  // slower inflate to try again to avoid locking up the guest.
  //
  // If we repeatedly get puff timeouts without an LMKD kill to end the
  // aggressive balloon, then we timeout the aggressive balloon. This is not
  // considered a failure, since the balloon should have at least accomplished
  // its goal of dropping as much guest page cache as possible.
  base::TimeDelta aggressive_balloon_interval_
      GUARDED_BY_CONTEXT(sequence_checker_);
  base::Thread balloon_request_thread_{"balloon_request_thread"};

  // Metrics reporter for virtio-blk performance.
  std::unique_ptr<VirtioBlkMetrics> virtio_blk_metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Metrics reporter for virtio-balloon events.
  std::unique_ptr<mm::BalloonMetrics> balloon_metrics_;

  base::WeakPtrFactory<ArcVm> weak_ptr_factory_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_ARC_VM_H_
