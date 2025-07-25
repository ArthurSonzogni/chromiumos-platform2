// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/arc_vm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/page_size.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/vm_tools.h>
#include <spaced/proto_bindings/spaced.pb.h>
#include <vboot/crossystem.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/balloon_policy.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/crosvm_control.h"
#include "vm_tools/concierge/network/arc_network.h"
#include "vm_tools/concierge/tap_device_builder.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vmm_swap_metrics.h"

namespace vm_tools::concierge {
namespace {

// Name of the control socket used for controlling crosvm.
constexpr char kCrosvmSocket[] = "arcvm.sock";

// How long to wait before timing out on child process exits.
constexpr base::TimeDelta kChildExitTimeout = base::Seconds(10);

// How long to sleep between arc-powerctl connection attempts.
constexpr base::TimeDelta kArcPowerctlConnectDelay = base::Milliseconds(250);

// How long to wait before giving up on connecting to arc-powerctl.
constexpr base::TimeDelta kArcPowerctlConnectTimeout = base::Seconds(5);

// Port for arc-powerctl running on the guest side.
constexpr unsigned int kVSockPort = 4242;

// Custom parameter key to skip all swap policy for ARCVM swap.
constexpr char kKeyToSkipSwapPolicy[] = "SKIP_SWAP_POLICY";

// Shared directories and their tags
constexpr char kOemEtcSharedDir[] = "/run/arcvm/host_generated/oem/etc";
constexpr char kOemEtcSharedDirTag[] = "oem_etc";

constexpr char kTestHarnessSharedDir[] = "/run/arcvm/testharness";
constexpr char kTestHarnessSharedDirTag[] = "testharness";

constexpr char kApkCacheSharedDir[] = "/run/arcvm/apkcache";
constexpr char kApkCacheSharedDirTag[] = "apkcache";

constexpr char kJemallocConfigFile[] = "/run/arcvm/ro/jemalloc/je_malloc.conf";
constexpr char kJemallocHighMemDeviceConfig[] =
    "narenas:12,tcache:true,lg_tcache_max:16";

constexpr char kReadonlySharedDir[] = "/run/arcvm/ro";
constexpr char kReadonlySharedDirTag[] = "ro";

// By default, treat 6GB+ devices as high-memory devices.
// The threshold is in MB and slightly less than 6000
// because the physical memory size of 6GB devices is
// usually slightly less than 6000MB.
// It can be changed with the Finch feature.
constexpr int kDefaultHighMemDeviceThreshold = 5500;

// For |kOemEtcSharedDir|, map host's crosvm to guest's root, also arc-camera
// (603) to vendor_arc_camera (5003).
constexpr char kOemEtcUgidMapTemplate[] = "0 %u 1, 5000 600 50";

// Constants for querying the ChromeOS channel
constexpr char kChromeOsReleaseTrack[] = "CHROMEOS_RELEASE_TRACK";
constexpr char kUnknown[] = "unknown";

constexpr const char kVbMetaDigestFileName[] =
    "/opt/google/vms/android/arcvm_vbmeta_digest.sha256";
constexpr uint32_t kExpectedVbMetaDigestSize = 64;

// The vmm-swap out should be skipped for 24 hours once it's done.
constexpr base::TimeDelta kVmmSwapOutCoolingDownPeriod = base::Hours(24);
// Vmm-swap trim should be triggered 10 minutes after enable to let hot pages of
// the guest move back to the guest memory.
constexpr base::TimeDelta kVmmSwapTrimWaitPeriod = base::Minutes(10);

// After shrinking via the aggressive balloon, ARCVM's size should be less
// than 1GiB. Since only the core Android services should be running at this
// point, this value is independent of the size of guest memory.
constexpr int64_t kExpectedMaxShrunkArcVmSize = GiB(1);

// ConnectVSock connects to arc-powerctl in the VM identified by |cid|. It
// returns a pair. The first object is the connected socket if connection was
// successful. The second is a bool that is true if the VM is already dead, and
// false otherwise.
std::pair<base::ScopedFD, bool> ConnectVSock(int cid) {
  DLOG(INFO) << "Creating VSOCK...";
  struct sockaddr_vm sa = {};
  sa.svm_family = AF_VSOCK;
  sa.svm_cid = cid;
  sa.svm_port = kVSockPort;

  base::ScopedFD fd(
      socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create VSOCK";
    return {base::ScopedFD(), false};
  }

  DLOG(INFO) << "Connecting VSOCK";
  if (HANDLE_EINTR(connect(fd.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    fd.reset();
    PLOG(ERROR) << "Failed to connect.";
    // When connect() returns ENODEV, this means the host kernel cannot find a
    // guest CID matching the address (VM is already dead). When connect returns
    // ETIMEDOUT, this means that the host kernel was able to send the connect
    // packet, but the guest does not respond within the timeout (VM is almost
    // dead). In these cases, return true so that the caller will stop retrying.
    return {base::ScopedFD(), (errno == ENODEV || errno == ETIMEDOUT)};
  }

  DLOG(INFO) << "VSOCK connected.";
  return {std::move(fd), false};
}

bool ShutdownArcVm(int cid) {
  base::ScopedFD vsock;
  const base::Time connect_deadline =
      base::Time::Now() + kArcPowerctlConnectTimeout;
  while (base::Time::Now() < connect_deadline) {
    bool vm_is_dead = false;
    std::tie(vsock, vm_is_dead) = ConnectVSock(cid);
    if (vsock.is_valid()) {
      break;
    }
    if (vm_is_dead) {
      DLOG(INFO) << "ARCVM is already gone.";
      return true;
    }
    base::PlatformThread::Sleep(kArcPowerctlConnectDelay);
  }

  if (!vsock.is_valid()) {
    return false;
  }

  const std::string command("poweroff");
  if (HANDLE_EINTR(write(vsock.get(), command.c_str(), command.size())) !=
      command.size()) {
    PLOG(WARNING) << "Failed to write to ARCVM VSOCK";
    return false;
  }

  DLOG(INFO) << "Started shutting down ARCVM";
  return true;
}

// Returns the value of ChromeOS channel From Lsb Release
// "unknown" if the value does not end with "-channel"
std::string GetChromeOsChannelFromLsbRelease() {
  const std::string kChannelSuffix = "-channel";
  std::string value;
  base::SysInfo::GetLsbReleaseValue(kChromeOsReleaseTrack, &value);

  if (!base::EndsWith(value, kChannelSuffix, base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Unknown ChromeOS channel: \"" << value << "\"";
    return kUnknown;
  }
  return value.erase(value.find(kChannelSuffix), kChannelSuffix.size());
}

// Returns the value of Verified Boot State based on developer mode.
// DeviceInfo expected values:
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/DeviceInfoV2.cddl
const char* DeriveVerifiedBootState(const bool dev_mode) {
  static constexpr char kVerifiedBoot[] = "green";
  static constexpr char kUnverifiedBoot[] = "orange";
  if (!dev_mode) {
    return kVerifiedBoot;
  }
  return kUnverifiedBoot;
}

// Devices in debug mode are considered unlocked since new
// software can be flashed and it does not enforce verification.
// Non-debug devices do not allow modification and must go through
// verified boot.
const char* DeriveBootloaderState(const bool dev_mode) {
  static constexpr char kLockedDevice[] = "locked";
  static constexpr char kUnlockedDevice[] = "unlocked";
  if (!dev_mode) {
    return kLockedDevice;
  }
  return kUnlockedDevice;
}

}  // namespace

SharedDirParam GetOemEtcSharedDirParam(uid_t euid, gid_t egid) {
  std::string oem_etc_uid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, euid);
  std::string oem_etc_gid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, egid);
  return SharedDirParam{.data_dir = base::FilePath(kOemEtcSharedDir),
                        .tag = kOemEtcSharedDirTag,
                        .uid_map = oem_etc_uid_map,
                        .gid_map = oem_etc_gid_map,
                        .enable_caches = SharedDirParam::Cache::kAlways};
}

ArcVm::ArcVm(Config config)
    : VmBaseImpl(VmBaseImpl::Config{
          .vsock_cid = config.vsock_cid,
          .network = std::move(config.network),
          .seneschal_server_proxy = std::move(config.seneschal_server_proxy),
          .cros_vm_socket = kCrosvmSocket,
          .runtime_dir = std::move(config.runtime_dir),
          .guest_memory_size = config.guest_memory_size,
      }),
      data_disk_path_(config.data_disk_path),
      features_(config.features),
      vmm_swap_metrics_(std::move(config.vmm_swap_metrics)),
      swap_policy_timer_(std::move(config.swap_policy_timer)),
      swap_state_monitor_timer_(std::move(config.swap_state_monitor_timer)),
      vmm_swap_low_disk_policy_(std::move(config.vmm_swap_low_disk_policy)),
      vmm_swap_tbw_policy_(config.vmm_swap_tbw_policy),
      vmm_swap_usage_policy_(config.vmm_swap_usage_path),
      vm_swapping_notify_callback_(
          std::move(config.vm_swapping_notify_callback)),
      virtio_blk_metrics_(std::move(config.virtio_blk_metrics)),
      weak_ptr_factory_(this) {
  if (config.is_vmm_swap_enabled) {
    vmm_swap_usage_policy_.Init();
  }
  vmm_swap_metrics_->SetFetchVmmSwapStatusFunction(
      base::BindRepeating(&ArcVm::FetchVmmSwapStatus, base::Unretained(this)));
}

ArcVm::~ArcVm() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vmm_swap_usage_policy_.OnDestroy();
  vmm_swap_metrics_->OnDestroy();

  Shutdown();
}

std::unique_ptr<ArcVm> ArcVm::Create(Config config) {
  auto kernel = std::move(config.kernel);
  auto vm_builder = std::move(config.vm_builder);

  auto vm = base::WrapUnique(new ArcVm(std::move(config)));

  if (!vm->Start(std::move(kernel), std::move(vm_builder))) {
    return {};
  }

  return vm;
}

bool ArcVm::Start(base::FilePath kernel, VmBuilder vm_builder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Open the tap device(s).
  bool no_tap_fd_added = true;
  for (const auto& tap : GetNetworkAllocation().tap_device_ifnames) {
    auto fd = OpenTapDevice(tap, true /*vnet_hdr*/, nullptr /*ifname_out*/);
    if (!fd.is_valid()) {
      LOG(ERROR) << "Unable to open and configure TAP device " << tap;
    } else {
      vm_builder.AppendTapFd(std::move(fd));
      no_tap_fd_added = false;
    }
  }

  if (no_tap_fd_added) {
    LOG(ERROR) << "No TAP devices available";
    return false;
  }

  if (USE_CROSVM_VIRTIO_VIDEO) {
    vm_builder.EnableVideoDecoder(true /* enable */)
        .EnableVideoEncoder(true /* enable */);
    if (USE_CROSVM_VIRTIO_VIDEO_VD) {
      vm_builder.SetVideoDecoder("libvda-vd");
    }
  }

  const base::FilePath jemalloc_config_file(kJemallocConfigFile);

  // Create a config symlink for memory-rich devices.
  int64_t sys_memory_mb = base::SysInfo::AmountOfPhysicalMemoryMB();

  // jemalloc_config_file might have been created on the
  // previous ARCVM boot. If the file already exists we do nothing.
  if ((sys_memory_mb >= kDefaultHighMemDeviceThreshold ||
       features_.low_mem_jemalloc_arenas_enabled) &&
      !base::IsLink(jemalloc_config_file)) {
    // This symbolic link does not point to any file. It is used as a string
    // which contains the allocator config.
    if (!base::CreateSymbolicLink(base::FilePath(kJemallocHighMemDeviceConfig),
                                  jemalloc_config_file)) {
      LOG(ERROR) << "Could not create a jemalloc config";
      return false;
    }
  }

  vm_builder
      // Bias tuned on 4/8G hatch devices with multivm.Lifecycle tests.
      .SetBalloonBias("48")
      .SetVsockCid(vsock_cid_)
      .SetSocketPath(GetVmSocketPath())
      .AddExtraWaylandSocket("/run/arcvm/mojo/mojo-proxy.sock,name=mojo")
      .EnableGpu(true /* enable */)
      .AppendAudioDevice(
          "capture=true,backend=cras,client_type=arcvm,"
          "socket_type=unified,num_input_devices=3,"
          "num_output_devices=4,"
          "output_device_config=[[],[],[],[stream_type=pro_audio]],"
          "input_device_config=[[],[],[stream_type=pro_audio]]")
      .AppendSharedDir(GetOemEtcSharedDirParam(geteuid(), getegid()))
      .AppendSharedDir(
          SharedDirParam{.data_dir = base::FilePath(kTestHarnessSharedDir),
                         .tag = kTestHarnessSharedDirTag,
                         .uid_map = kAndroidUidMap,
                         .gid_map = kAndroidGidMap,
                         .enable_caches = SharedDirParam::Cache::kAlways,
                         .ascii_casefold = false,
                         .posix_acl = true})
      .AppendSharedDir(
          SharedDirParam{.data_dir = base::FilePath(kApkCacheSharedDir),
                         .tag = kApkCacheSharedDirTag,
                         .uid_map = kAndroidUidMap,
                         .gid_map = kAndroidGidMap,
                         .enable_caches = SharedDirParam::Cache::kAlways,
                         .ascii_casefold = false,
                         .posix_acl = true})
      .AppendSharedDir(
          SharedDirParam{.data_dir = base::FilePath(kReadonlySharedDir),
                         .tag = kReadonlySharedDirTag,
                         .uid_map = kAndroidUidMap,
                         .gid_map = kAndroidGidMap,
                         .enable_caches = SharedDirParam::Cache::kAlways,
                         .ascii_casefold = false,
                         .posix_acl = true})
      .EnableBattery(true /* enable */)
      .EnableDelayRt(true /* enable */);

  if (USE_CROSVM_VULKAN) {
    vm_builder.EnableVulkan(true).EnableRenderServer(true);
  }

  // reset context-type choices, then set explicitly
  vm_builder.EnableGpuContextTypeDefaults();
  if (USE_CROSVM_CROSS_DOMAIN_CONTEXT) {
    vm_builder.EnableGpuContextTypeCrossDomain(true);
  } else {
    vm_builder.EnableGpuContextTypeVirgl(true);
  }
  vm_builder.EnableGpuContextTypeVenus(USE_CROSVM_VULKAN);
  vm_builder.EnableGpuContextTypeDrm(USE_CROSVM_VIRTGPU_NATIVE_CONTEXT);

  std::unique_ptr<CustomParametersForDev> custom_parameters =
      MaybeLoadCustomParametersForDev(apps::ARCVM, use_dev_conf());

  if (custom_parameters &&
      custom_parameters->ObtainSpecialParameter(kKeyToSkipSwapPolicy)
              .value_or("false") == "true") {
    skip_swap_policy_ = true;
  }

  // Finally set the path to the kernel.
  vm_builder.SetKernel(std::move(kernel));

  std::optional<base::StringPairs> args =
      std::move(vm_builder).BuildVmArgs(custom_parameters.get());
  if (!args) {
    LOG(ERROR) << "Failed to build VM arguments";
    return false;
  }

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for ARCVM's crosvm processes. Note that once crosvm starts,
  // crosvm adds its vCPU threads to the kArcvmVcpuCpuCgroup by itself.
  process_.SetPreExecCallback(base::BindOnce(
      &SetUpCrosvmProcess, base::FilePath(kArcvmCpuCgroup).Append("tasks")));

  if (!StartProcess(std::move(args).value())) {
    LOG(ERROR) << "Failed to start VM process";
    return false;
  }

  return true;
}

bool ArcVm::Shutdown() {
  // Do a check here to make sure the process is still around.  It may have
  // crashed and we don't want to be waiting around for an RPC response that's
  // never going to come.  kill with a signal value of 0 is explicitly
  // documented as a way to check for the existence of a process.
  if (!CheckProcessExists(process_.pid())) {
    LOG(INFO) << "ARCVM process is already gone. Do nothing";
    process_.Release();
    return true;
  }

  LOG(INFO) << "Shutting down ARCVM";
  if (ShutdownArcVm(vsock_cid_)) {
    if (WaitForChild(process_.pid(), kChildExitTimeout)) {
      LOG(INFO) << "ARCVM is shut down";
      process_.Release();
      return true;
    }
    LOG(WARNING) << "Timed out waiting for ARCVM to shut down.";
  }
  LOG(WARNING) << "Failed to shut down ARCVM gracefully.";

  LOG(WARNING) << "Trying to shut ARCVM down via the crosvm socket.";
  Stop();

  // We can't actually trust the exit codes that crosvm gives us so just see if
  // it exited.
  if (WaitForChild(process_.pid(), kChildExitTimeout)) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to stop VM " << vsock_cid_ << " via crosvm socket";

  // Kill the process with SIGTERM.
  if (process_.Kill(SIGTERM, kChildExitTimeout.InSeconds())) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to kill VM " << vsock_cid_ << " with SIGTERM";

  // Kill it with fire.
  if (process_.Kill(SIGKILL, kChildExitTimeout.InSeconds())) {
    process_.Release();
    return true;
  }

  LOG(ERROR) << "Failed to kill VM " << vsock_cid_ << " with SIGKILL";
  return false;
}

bool ArcVm::AttachNetDevice(const std::string& tap_name, uint8_t* out_bus) {
  return vm_tools::concierge::AttachNetDevice(GetVmSocketPath(), tap_name,
                                              out_bus);
}

bool ArcVm::DetachNetDevice(uint8_t bus) {
  return vm_tools::concierge::DetachNetDevice(GetVmSocketPath(), bus);
}

const std::unique_ptr<BalloonPolicyInterface>& ArcVm::GetBalloonPolicy(
    uint64_t critical_margin, const std::string& vm) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  static const std::unique_ptr<BalloonPolicyInterface> null_balloon_policy;
  return null_balloon_policy;
}

void ArcVm::HandleSuspendImminent() {
  SuspendCrosvm();
}

void ArcVm::HandleSuspendDone() {
  ResumeCrosvm();
}

void ArcVm::HandleUserlandReady() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Create the RT v-Cpu for the VM now that boot is complete.
  MakeRtVcpu();

  virtio_blk_metrics_->ReportBootMetrics(apps::VmType::ARCVM, vsock_cid_);
  virtio_blk_metrics_->ScheduleDailyMetrics(apps::VmType::ARCVM, vsock_cid_);
}

// static
bool ArcVm::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                int quota) {
  bool ret = true;
  if (!VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                       kArcvmCpuCgroup)) {
    ret = false;
  }
  if (!VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                       kArcvmVcpuCpuCgroup)) {
    ret = false;
  }

  switch (cpu_restriction_state) {
    case CPU_RESTRICTION_FOREGROUND:
    case CPU_RESTRICTION_BACKGROUND:
      // Reset/remove the quota. Needed to handle the case where user signs out
      // before quota was reset.
      quota = kCpuPercentUnlimited;
      break;
    case CPU_RESTRICTION_BACKGROUND_WITH_CFS_QUOTA_ENFORCED:
      break;
    default:
      NOTREACHED();
  }

  // Apply quotas.
  if (!UpdateCpuQuota(base::FilePath(kArcvmCpuCgroup), quota)) {
    ret = false;
  }
  if (!UpdateCpuQuota(base::FilePath(kArcvmVcpuCpuCgroup), quota)) {
    ret = false;
  }

  return ret;
}

uint32_t ArcVm::IPv4Address() const {
  return GetNetworkAllocation().arc0_ipv4_address.ToInAddr().s_addr;
}

VmBaseImpl::Info ArcVm::GetInfo() const {
  VmBaseImpl::Info info = {
      .ipv4_address = IPv4Address(),
      .pid = pid(),
      .cid = cid(),
      .seneschal_server_handle = seneschal_server_handle(),
      .status = VmBaseImpl::Status::RUNNING,
      .type = apps::VmType::ARCVM,
  };

  return info;
}

bool ArcVm::GetVmEnterpriseReportingInfo(
    GetVmEnterpriseReportingInfoResponse* response) {
  response->set_success(false);
  response->set_failure_reason("Not implemented");
  return false;
}

vm_tools::concierge::DiskImageStatus ArcVm::ResizeDisk(
    uint64_t new_size, std::string* failure_reason) {
  if (data_disk_path_.empty()) {
    *failure_reason = "Disk doesn't exist";
    LOG(ERROR) << "ArcVm::ResizeDisk failed: " << *failure_reason;
    return DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST;
  }

  std::optional<int64_t> raw_current_size = base::GetFileSize(data_disk_path_);
  if (!raw_current_size.has_value()) {
    *failure_reason = "Unable to get current disk size";
    LOG(ERROR) << "ArcVm::ResizeDisk failed: " << *failure_reason;
    return DiskImageStatus::DISK_STATUS_FAILED;
  }
  int64_t current_size = raw_current_size.value();

  LOG(INFO) << "ArcVm::ResizeDisk: current_size=" << current_size
            << " requested_size=" << new_size;

  if (new_size == current_size) {
    LOG(INFO) << "ArcVm::ResizeDisk: Disk is already requested size";
    return DiskImageStatus::DISK_STATUS_RESIZED;
  }

  if (new_size < current_size) {
    *failure_reason = "Disk shrinking is not supported yet";
    LOG(ERROR) << "ArcVm::ResizeDisk failed: " << *failure_reason;
    return DiskImageStatus::DISK_STATUS_FAILED;
  }

  DCHECK_GT(new_size, current_size);

  // CrosvmDiskResize takes a 1-based index.
  if (!CrosvmDiskResize(GetVmSocketPath(), kDataDiskIndex + 1, new_size)) {
    *failure_reason = "\"crosvm disk resize\" failed";
    LOG(ERROR) << "ArcVm::ResizeDisk failed: " << *failure_reason;
    return DiskImageStatus::DISK_STATUS_FAILED;
  }

  LOG(INFO) << "ArcVm::ResizeDisk succeeded";
  return DiskImageStatus::DISK_STATUS_RESIZED;
}

vm_tools::concierge::DiskImageStatus ArcVm::GetDiskResizeStatus(
    std::string* failure_reason) {
  // No need to implement this for now because ArcVm::ResizeDisk synchronously
  // executes the resizing operation.
  // We will need to implement this when we support asynchronous disk resizing.
  *failure_reason = "Not implemented";
  return DiskImageStatus::DISK_STATUS_FAILED;
}

void ArcVm::HandleSwapVmRequest(const SwapVmRequest& request,
                                SwapVmCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SuccessFailureResponse response;
  switch (request.operation()) {
    case SwapOperation::ENABLE:
      LOG(INFO) << "Enable vmm-swap";
      HandleSwapVmEnableRequest(std::move(callback));
      return;

    case SwapOperation::FORCE_ENABLE:
      LOG(INFO) << "Force enable vmm-swap";
      HandleSwapVmForceEnableRequest(response);
      break;

    case SwapOperation::DISABLE:
      LOG(INFO) << "Disable vmm-swap";
      HandleSwapVmDisableRequest(response);
      break;

    default:
      LOG(WARNING) << "Undefined vmm-swap operation";
      response.set_success(false);
      response.set_failure_reason("Unknown operation");
      break;
  }
  std::move(callback).Run(response);
}

void ArcVm::HandleStatefulUpdate(const spaced::StatefulDiskSpaceUpdate update) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Should not disable vmm-swap if vmm-swap is not enabled because there is a
  // case when vmm-swap is not available. StatefulDiskSpaceUpdate arrives
  // independent from vmm-swap.
  if (update.state() != spaced::StatefulDiskSpaceState::LOW &&
      update.state() != spaced::StatefulDiskSpaceState::CRITICAL) {
    return;
  }

  if (skip_swap_policy_) {
    return;
  }

  if (is_vmm_swap_enabled_ || requested_slow_file_cleanup_) {
    LOG(INFO) << "Disable vmm-swap due to low disk notification";
    if (!DisableVmmSwap(VmmSwapDisableReason::kLowDiskSpace, false)) {
      LOG(ERROR) << "Failure on crosvm swap command for disable";
    }
  }
}

base::TimeDelta ArcVm::CalculateVmmSwapDurationTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  double tbw_target_per_day =
      static_cast<double>(vmm_swap_tbw_policy_->GetTargetTbwPerDay());
  if (tbw_target_per_day <= 0) {
    return base::Days(28);
  }
  // Swapping ARCVM will require writing less than this much data in the vast
  // majority of cases. In the rare case that we end up writing too much data,
  // the TBW policy will end up preventing swap for the next few days until the
  // running TBW cost falls below the weekly and monthly thresholds.
  double factor = kExpectedMaxShrunkArcVmSize / tbw_target_per_day;
  if (factor > 28) {
    return base::Days(28);
  }
  double target_seconds = factor * base::Hours(24).InSecondsF();
  return base::Seconds(static_cast<int64_t>(target_seconds));
}

void ArcVm::HandleSwapVmEnableRequest(SwapVmCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vmm_swap_metrics_->OnSwappableIdleEnabled();
  vmm_swap_usage_policy_.OnEnabled();

  if (!pending_swap_vm_callback_.is_null()) {
    SuccessFailureResponse response;
    response.set_failure_reason("Previous enable request is being executed");
    std::move(callback).Run(response);
    return;
  }

  if (is_vmm_swap_enabled_) {
    if ((base::Time::Now() - last_vmm_swap_out_at_) <
        kVmmSwapOutCoolingDownPeriod) {
      LOG(INFO) << "Skip enabling vmm-swap for maintenance for "
                << kVmmSwapOutCoolingDownPeriod;
      ApplyVmmSwapPolicyResult(std::move(callback),
                               VmmSwapPolicyResult::kCoolDown);
      return;
    }
  } else {
    base::TimeDelta min_vmm_swap_duration_target =
        CalculateVmmSwapDurationTarget();
    base::TimeDelta next_disable_duration =
        vmm_swap_usage_policy_.PredictDuration();
    if (!skip_swap_policy_ &&
        next_disable_duration < min_vmm_swap_duration_target) {
      LOG(INFO) << "Enabling vmm-swap is rejected by usage prediction. "
                   "Predict duration: "
                << next_disable_duration << " should be longer than "
                << min_vmm_swap_duration_target;
      ApplyVmmSwapPolicyResult(std::move(callback),
                               VmmSwapPolicyResult::kUsagePrediction);
      return;
    }
  }
  if (!skip_swap_policy_ && !vmm_swap_tbw_policy_->CanSwapOut()) {
    LOG(WARNING) << "Enabling vmm-swap is rejected by TBW limit";
    ApplyVmmSwapPolicyResult(
        std::move(callback),
        VmmSwapPolicyResult::kExceededTotalBytesWrittenLimit);
    return;
  }

  if (!is_vmm_swap_enabled_ && !skip_swap_policy_) {
    pending_swap_vm_callback_ = std::move(callback);
    vmm_swap_low_disk_policy_->CanEnable(
        *guest_memory_size_,
        base::BindOnce(&ArcVm::OnVmmSwapLowDiskPolicyResult,
                       base::Unretained(this)));
  } else {
    ApplyVmmSwapPolicyResult(std::move(callback),
                             VmmSwapPolicyResult::kApprove);
  }
}

void ArcVm::OnVmmSwapLowDiskPolicyResult(bool can_enable) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // `pending_swap_vm_callback_` can be nullopt when vmm-swap is disabled while
  // it is waiting for a result from VmmSwapLowDiskPolicy.
  // When consecutive requests (1) Enable (2) Disable (3) Enable arrive in a
  // very short time, there can be a rare case that `pending_swap_vm_callback_`
  // for (3) is present when VmmSwapLowDiskPolicy for (1) triggers an obsolete
  // result. However responding to `pending_swap_vm_callback_` with an obsolete
  // VmmSwapLowDiskPolicy result is not a problem because the disk free space
  // unlikely change in the short time.
  if (!pending_swap_vm_callback_.is_null()) {
    if (!can_enable) {
      LOG(INFO) << "Enabling vmm-swap is rejected by low disk mode.";
    }
    ApplyVmmSwapPolicyResult(std::move(pending_swap_vm_callback_),
                             can_enable ? VmmSwapPolicyResult::kApprove
                                        : VmmSwapPolicyResult::kLowDisk);
  }
}

void ArcVm::ApplyVmmSwapPolicyResult(SwapVmCallback callback,
                                     VmmSwapPolicyResult policy_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  vmm_swap_metrics_->ReportPolicyResult(policy_result, !is_vmm_swap_enabled_);

  SuccessFailureResponse response;
  if (policy_result == VmmSwapPolicyResult::kApprove ||
      (is_vmm_swap_enabled_ && !swap_policy_timer_->IsRunning())) {
    if (!CrosvmControl::Get()->EnableVmmSwap(GetVmSocketPath())) {
      LOG(ERROR) << "Failure on crosvm swap command for enable";
      response.set_failure_reason("Failure on crosvm swap command for enable");
      std::move(callback).Run(response);
      return;
    }
    if (policy_result == VmmSwapPolicyResult::kApprove) {
      vmm_swap_metrics_->OnVmmSwapEnabled();
      is_vmm_swap_enabled_ = true;
      swap_policy_timer_->Start(FROM_HERE, kVmmSwapTrimWaitPeriod, this,
                                &ArcVm::StartVmmSwapOut);
    } else {
      // Even if it is not allowed to vmm-swap out memory to swap file, it worth
      // doing vmm-swap trim. The trim command drops the zero/static pages
      // faulted into the guest memory since the last vmm-swap out.
      swap_policy_timer_->Start(FROM_HERE, kVmmSwapTrimWaitPeriod, this,
                                &ArcVm::TrimVmmSwapMemory);
    }
  }
  switch (policy_result) {
    case VmmSwapPolicyResult::kApprove:
      response.set_success(true);
      break;
    case VmmSwapPolicyResult::kCoolDown:
      response.set_failure_reason(
          "Requires cooling down period after last vmm-swap out");
      break;
    case VmmSwapPolicyResult::kUsagePrediction:
      response.set_failure_reason("Predicted disable soon");
      break;
    case VmmSwapPolicyResult::kExceededTotalBytesWrittenLimit:
      response.set_failure_reason("TBW (total bytes written) reached target");
      break;
    case VmmSwapPolicyResult::kLowDisk:
      response.set_failure_reason("Low disk mode");
      break;
    default:
      LOG(ERROR) << "Unexpected policy result: " << policy_result;
      response.set_failure_reason("Unexpected reason");
      break;
  }
  std::move(callback).Run(response);
}

void ArcVm::HandleSwapVmForceEnableRequest(SuccessFailureResponse& response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (CrosvmControl::Get()->EnableVmmSwap(GetVmSocketPath())) {
    vmm_swap_metrics_->OnVmmSwapEnabled();
    is_vmm_swap_enabled_ = true;
    response.set_success(true);
    swap_policy_timer_->Start(FROM_HERE, base::Seconds(10), this,
                              &ArcVm::StartVmmSwapOut);
  } else {
    LOG(ERROR) << "Failure on crosvm swap command for force-enable";
    response.set_success(false);
    response.set_failure_reason(
        "Failure on crosvm swap command for force-enable");
  }
}

void ArcVm::HandleSwapVmDisableRequest(SuccessFailureResponse& response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vmm_swap_usage_policy_.OnDisabled();
  if (DisableVmmSwap(VmmSwapDisableReason::kDisableRequest, true)) {
    response.set_success(true);
  } else {
    LOG(ERROR) << "Failure on crosvm swap command for disable";
    response.set_failure_reason("Failure on crosvm swap command for disable");
  }
  vmm_swap_metrics_->OnSwappableIdleDisabled();
}

bool ArcVm::DisableVmmSwap(VmmSwapDisableReason reason,
                           bool slow_file_cleanup) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (swap_policy_timer_->IsRunning()) {
    LOG(INFO) << "Cancel pending swap out";
    swap_policy_timer_->Stop();
  }
  if (swap_state_monitor_timer_->IsRunning()) {
    LOG(INFO) << "Cancel swap state monitor";
    swap_state_monitor_timer_->Stop();
  }
  if (!pending_swap_vm_callback_.is_null()) {
    LOG(INFO) << "Cancel pending enable vmm-swap";
    SuccessFailureResponse response;
    response.set_failure_reason("Aborted on disable vmm-swap");
    std::move(pending_swap_vm_callback_).Run(response);
  }
  is_vmm_swap_enabled_ = false;
  requested_slow_file_cleanup_ = slow_file_cleanup;
  vmm_swap_metrics_->OnVmmSwapDisabled(reason);
  vm_swapping_notify_callback_.Run(SWAPPING_IN);
  return CrosvmControl::Get()->DisableVmmSwap(GetVmSocketPath(),
                                              slow_file_cleanup);
}

void ArcVm::TrimVmmSwapMemory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Trim vmm-swap memory";
  if (!CrosvmControl::Get()->VmmSwapTrim(GetVmSocketPath())) {
    LOG(ERROR) << "Failed to start vmm-swap trim";
  }
}

void ArcVm::StartVmmSwapOut() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Start vmm-swap trim";
  if (CrosvmControl::Get()->VmmSwapTrim(GetVmSocketPath())) {
    swap_state_monitor_timer_->Start(FROM_HERE, base::Milliseconds(1000), this,
                                     &ArcVm::RunVmmSwapOutAfterTrim);
  } else {
    LOG(ERROR) << "Failed to start vmm-swap trim";
  }
}

void ArcVm::RunVmmSwapOutAfterTrim() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  struct SwapStatus status;
  if (!CrosvmControl::Get()->VmmSwapStatus(GetVmSocketPath(), &status)) {
    LOG(INFO) << "Failed to get vmm-swap state";
    swap_state_monitor_timer_->Stop();
    return;
  }
  switch (status.state) {
    case SwapState::TRIM_IN_PROGRESS:
      // do nothing and wait next monitor
      break;
    case SwapState::PENDING:
      LOG(INFO) << "Vmm-swap out";
      swap_state_monitor_timer_->Stop();

      // The actual bytes written into the swap file is less than or equal to
      // (and in most cases similar to) the staging memory size. This may be a
      // little pessimistic as to how many bytes are actually written, but it's
      // simpler than dealing with the rare cases where the swap out operation
      // fails or needs to be aborted.
      vmm_swap_tbw_policy_->Record(status.metrics.staging_pages *
                                   base::GetPageSize());
      vmm_swap_metrics_->OnPreVmmSwapOut(status.metrics.staging_pages);

      vm_swapping_notify_callback_.Run(SWAPPING_OUT);
      CrosvmControl::Get()->VmmSwapOut(GetVmSocketPath());
      last_vmm_swap_out_at_ = base::Time::Now();
      return;
    default:
      LOG(INFO) << "Unexpected trim result" << status.state;
      swap_state_monitor_timer_->Stop();
      return;
  }
}

base::expected<SwapStatus, std::string> ArcVm::FetchVmmSwapStatus() {
  SwapStatus status;
  if (!CrosvmControl::Get()->VmmSwapStatus(GetVmSocketPath(), &status)) {
    return base::unexpected("crosvm command error");
  }
  return status;
}

const patchpanel::Client::ArcVMAllocation& ArcVm::GetNetworkAllocation() const {
  return static_cast<ArcNetwork*>(GetNetwork())->Allocation();
}

// static
std::vector<std::string> ArcVm::GetKernelParams(
    const crossystem::Crossystem& cros_system,
    const StartArcVmRequest& request,
    int seneschal_server_port) {
  // Build the plugin params.
  bool is_dev_mode = cros_system.VbGetSystemPropertyInt("cros_debug") == 1;
  // Whether the host is on VM or not.
  bool is_host_on_vm = cros_system.VbGetSystemPropertyInt("inside_vm") == 1;
  std::string channel = GetChromeOsChannelFromLsbRelease();
  arc::StartArcMiniInstanceRequest mini_instance_request =
      request.mini_instance_request();

  const char* vb_device_state = DeriveBootloaderState(is_dev_mode);
  const char* verified_boot_state = DeriveVerifiedBootState(is_dev_mode);
  const std::optional<std::vector<uint8_t>> vbmeta_digest_opt =
      GetVbMetaDigestFromFile(base::FilePath(kVbMetaDigestFileName));

  int64_t zram_size = MiB(request.guest_zram_mib());

  std::vector<std::string> params = {
      "root=/dev/vda",
      "init=/init",
      // Note: Do not change the value "bertha". This string is checked in
      // platform2/resourced/src/process_stats.rs to detect ARCVM's crosvm
      // processes, for example.
      "androidboot.hardware=bertha",
      "androidboot.container=1",
      base::StringPrintf("androidboot.dev_mode=%d", is_dev_mode),
      "androidboot.chromeos_channel=" + channel,
      base::StringPrintf("androidboot.seneschal_server_port=%d",
                         seneschal_server_port),
      base::StringPrintf("androidboot.lcd_density=%d",
                         mini_instance_request.lcd_density()),
      "androidboot.arc.primary_display_rotation=" +
          StartArcVmRequest::DisplayOrientation_Name(
              request.panel_orientation()),
      // Disable panicking on softlockup since it can be false-positive on VMs.
      // See http://b/235866242#comment23 for the context.
      // TODO(b/241051098): Re-enable it once this workaround is not needed.
      "softlockup_panic=0",
      "androidboot.enable_consumer_auto_update_toggle=" +
          std::to_string(
              mini_instance_request.enable_consumer_auto_update_toggle()),
      "androidboot.enable_privacy_hub_for_chrome=" +
          std::to_string(mini_instance_request.enable_privacy_hub_for_chrome()),
      base::StringPrintf("androidboot.arcvm_virtio_blk_data=%d",
                         request.enable_virtio_blk_data()),
      base::StringPrintf("androidboot.arcvm.data_block_io_scheduler=%d",
                         request.enable_data_block_io_scheduler()),
      base::StringPrintf("androidboot.arc_switch_to_keymint=%d",
                         mini_instance_request.arc_switch_to_keymint()),
      base::StringPrintf("androidboot.enable_arc_attestation=%d",
                         mini_instance_request.enable_arc_attestation()),
      base::StringPrintf("androidboot.arc.signed_in=%d",
                         mini_instance_request.arc_signed_in()),
      base::StringPrintf("androidboot.verifiedbootstate=%s",
                         verified_boot_state),
      base::StringPrintf("androidboot.vbmeta.device_state=%s", vb_device_state),
      // Avoid the RCU synchronization from blocking. See b/285791678#comment74
      // for the context.
      "rcupdate.rcu_expedited=1",
      "rcutree.kthread_prio=1",
  };

  if (vbmeta_digest_opt.has_value()) {
    params.push_back(base::StringPrintf(
        "androidboot.vbmeta.digest=%s",
        brillo::BlobToString(vbmeta_digest_opt.value()).c_str()));
  }

  if (is_host_on_vm) {
    params.push_back("androidboot.host_is_in_vm=1");
  }

  if (!is_dev_mode) {
    params.push_back("androidboot.disable_runas=1");
  }

  if (mini_instance_request.arc_custom_tabs_experiment()) {
    params.push_back("androidboot.arc_custom_tabs=1");
  }

  if (zram_size) {
    params.push_back(
        base::StringPrintf("androidboot.zram_size=%" PRId64, zram_size));
  }

  if (request.enable_s2idle()) {
    params.push_back("androidboot.arc.s2idle=1");
    // Make the default mem sleep state standby instead of freeze, so that the
    // guest clock is paused while suspended.
    params.push_back("mem_sleep_default=shallow");
  }

  auto mglru_reclaim_interval = request.mglru_reclaim_interval();
  if (mglru_reclaim_interval > 0) {
    params.push_back("androidboot.arcvm_mglru_reclaim_interval=" +
                     std::to_string(mglru_reclaim_interval));
    auto mglru_reclaim_swappiness = request.mglru_reclaim_swappiness();
    if (mglru_reclaim_swappiness >= 0) {
      params.push_back("androidboot.arcvm_mglru_reclaim_swappiness=" +
                       std::to_string(mglru_reclaim_swappiness));
    }
  }
  LOG(INFO) << base::StringPrintf("Setting ARCVM guest's zram size to %" PRId64,
                                  zram_size);

  if (request.enable_web_view_zygote_lazy_init()) {
    params.push_back("androidboot.arc.web_view_zygote.lazy_init=1");
  }
  if (request.rootfs_writable()) {
    params.push_back("rw");
  }

  auto guest_swappiness = request.guest_swappiness();
  if (guest_swappiness > 0) {
    params.push_back(
        base::StringPrintf("sysctl.vm.swappiness=%d", guest_swappiness));
  }

  // We run vshd under a restricted domain on non-test images.
  // (go/arcvm-android-sh-restricted)
  if (channel == "testimage") {
    params.push_back("androidboot.vshd_service_override=vshd_for_test");
  }
  params.push_back("androidboot.arc.broadcast_anr_prenotify=1");
  if (request.vm_memory_psi_period() >= 0) {
    // Since Android performs parameter validation, not doing it here.
    params.push_back(
        base::StringPrintf("androidboot.arcvm_metrics_mem_psi_period=%d",
                           request.vm_memory_psi_period()));
  }

  switch (request.ureadahead_mode()) {
    case vm_tools::concierge::StartArcVmRequest::UREADAHEAD_MODE_DISABLED:
      break;
    case vm_tools::concierge::StartArcVmRequest::UREADAHEAD_MODE_READAHEAD:
      params.push_back("androidboot.arcvm_ureadahead_mode=readahead");
      break;
    case vm_tools::concierge::StartArcVmRequest::UREADAHEAD_MODE_GENERATE:
      params.push_back("androidboot.arcvm_ureadahead_mode=generate");
      break;
    default:
      LOG(WARNING) << "WARNING: Invalid ureadahead mode ignored: ["
                   << request.ureadahead_mode() << "]";
      break;
  }

  switch (request.native_bridge_experiment()) {
    case vm_tools::concierge::StartArcVmRequest::BINARY_TRANSLATION_TYPE_NONE:
      params.push_back("androidboot.native_bridge=0");
      break;
    case vm_tools::concierge::StartArcVmRequest::
        BINARY_TRANSLATION_TYPE_HOUDINI:
      params.push_back("androidboot.native_bridge=libhoudini.so");
      break;
    case vm_tools::concierge::StartArcVmRequest::
        BINARY_TRANSLATION_TYPE_NDK_TRANSLATION:
      params.push_back("androidboot.native_bridge=libndk_translation.so");
      break;
    default:
      LOG(WARNING) << "WARNING: Invalid Native Bridge ignored: ["
                   << request.native_bridge_experiment() << "]";
      break;
  }

  switch (request.usap_profile()) {
    case vm_tools::concierge::StartArcVmRequest::USAP_PROFILE_DEFAULT:
      break;
    case vm_tools::concierge::StartArcVmRequest::USAP_PROFILE_4G:
      params.push_back("androidboot.usap_profile=4G");
      break;
    case vm_tools::concierge::StartArcVmRequest::USAP_PROFILE_8G:
      params.push_back("androidboot.usap_profile=8G");
      break;
    case vm_tools::concierge::StartArcVmRequest::USAP_PROFILE_16G:
      params.push_back("androidboot.usap_profile=16G");
      break;
    default:
      LOG(WARNING) << "WARNING: Invalid USAP Profile ignored: ["
                   << request.usap_profile() << "]";
      break;
  }

  if (mini_instance_request.arc_generate_pai()) {
    params.push_back("androidboot.arc_generate_pai=1");
  }
  if (mini_instance_request.disable_download_provider()) {
    params.push_back("androidboot.disable_download_provider=1");
  }
  // Only add boot property if flag to disable media store maintenance is set.
  if (mini_instance_request.disable_media_store_maintenance()) {
    params.push_back("androidboot.disable_media_store_maintenance=1");
    LOG(INFO) << "MediaStore maintenance task(s) are disabled";
  }
  if (mini_instance_request.enable_tts_caching()) {
    params.push_back("androidboot.arc.tts.caching=1");
  }

  switch (mini_instance_request.play_store_auto_update()) {
    case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_DEFAULT:
      break;
    case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_ON:
      params.push_back("androidboot.play_store_auto_update=1");
      break;
    case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_OFF:
      params.push_back("androidboot.play_store_auto_update=0");
      break;
    default:
      LOG(WARNING) << "WARNING: Invalid Auto Update type ignored: ["
                   << mini_instance_request.play_store_auto_update() << "]";
      break;
  }

  switch (mini_instance_request.dalvik_memory_profile()) {
    case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_DEFAULT:
    case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_4G:
      // Use the 4G profile for devices with 4GB RAM or less.
      params.push_back("androidboot.arc_dalvik_memory_profile=4G");
      break;
    case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_8G:
      params.push_back("androidboot.arc_dalvik_memory_profile=8G");
      break;
    case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_16G:
      params.push_back("androidboot.arc_dalvik_memory_profile=16G");
      break;
    default:
      LOG(WARNING) << "WARNING: Invalid Dalvik memory profile type ignored: ["
                   << mini_instance_request.dalvik_memory_profile() << "]";
      break;
  }

  // Only force a particular value if one is set. Otherwise the board
  // configuration may set it.
  if (mini_instance_request.force_max_acquired_buffers_experiment() > 0) {
    params.push_back(base::StringPrintf(
        "androidboot.vendor.arc.sf.maxacquired=%d",
        mini_instance_request.force_max_acquired_buffers_experiment()));
  }

  return params;
}

// static
std::optional<std::vector<uint8_t>> ArcVm::GetVbMetaDigestFromFile(
    const base::FilePath& vbmeta_digest_file_path) {
  std::string vbmeta_digest;
  if (!base::PathExists(vbmeta_digest_file_path)) {
    LOG(ERROR) << "VB Meta digest file does not exist at "
               << vbmeta_digest_file_path;
    return std::nullopt;
  }
  if (!base::ReadFileToString(base::FilePath(vbmeta_digest_file_path),
                              &vbmeta_digest)) {
    // In case of failure to read vb meta digest into string, return nullopt.
    LOG(ERROR) << "Failed to read vb meta digest file from path "
               << vbmeta_digest_file_path;
    return std::nullopt;
  }
  std::vector<uint8_t> vbmeta_digest_result =
      brillo::BlobFromString(vbmeta_digest);
  if (vbmeta_digest_result.size() != kExpectedVbMetaDigestSize) {
    LOG(ERROR) << "vbmeta digest is not a valid hash. "
               << "Expected size: " << kExpectedVbMetaDigestSize
               << ". Actual size: " << vbmeta_digest_result.size();
    return std::nullopt;
  }

  return vbmeta_digest_result;
}

}  // namespace vm_tools::concierge
