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

constexpr char kJemallocConfigFile[] = "/run/arcvm/jemalloc/je_malloc.conf";
constexpr char kJemallocSharedDirTag[] = "jemalloc";
constexpr char kJemallocHighMemDeviceConfig[] =
    "narenas:12,tcache:true,lg_tcache_max:16";

#if defined(__x86_64__) || defined(__aarch64__)
constexpr char kLibSharedDir[] = "/lib64";
constexpr char kUsrLibSharedDir[] = "/usr/lib64";
constexpr char kUsrLocalLibSharedDir[] = "/usr/local/lib64";
#else
constexpr char kLibSharedDir[] = "/lib";
constexpr char kUsrLibSharedDir[] = "/usr/lib";
constexpr char kUsrLocalLibSharedDir[] = "/usr/local/lib";
#endif
constexpr char kLibSharedDirTag[] = "lib";
constexpr char kUsrLibSharedDirTag[] = "usr_lib";
constexpr char kUsrLocalLibSharedDirTag[] = "usr_local_lib";

constexpr char kSbinSharedDir[] = "/sbin";
constexpr char kSbinSharedDirTag[] = "sbin";

constexpr char kUsrBinSharedDir[] = "/usr/bin";
constexpr char kUsrBinSharedDirTag[] = "usr_bin";

constexpr char kUsrLocalBinSharedDir[] = "/usr/local/bin";
constexpr char kUsrLocalBinSharedDirTag[] = "usr_local_bin";

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

// The amount of time after VM creation that we should wait to refresh counters
// bassed on the zone watermarks, since they can change during boot.
constexpr base::TimeDelta kBalloonRefreshTime = base::Seconds(60);

// The vmm-swap out should be skipped for 24 hours once it's done.
constexpr base::TimeDelta kVmmSwapOutCoolingDownPeriod = base::Hours(24);
// Vmm-swap trim should be triggered 10 minutes after enable to let hot pages of
// the guest move back to the guest memory.
constexpr base::TimeDelta kVmmSwapTrimWaitPeriod = base::Minutes(10);

// The default initialization parameters for ARCVM's LimitCacheBalloonPolicy
static constexpr LimitCacheBalloonPolicy::Params kArcVmLimitCachePolicyParams =
    {
        .reclaim_target_cache = KiB(322'560),
        .critical_target_cache = KiB(322'560),
        .moderate_target_cache = 0,
        .responsive_max_deflate_bytes = MiB(256),
};

int GetIntFromVsockBuffer(const uint8_t* buf, size_t index) {
  int ret = 0;
  std::memcpy(&ret, &buf[index * sizeof(int)], sizeof(int));
  ret = ntohl(ret);
  return ret;
}

void SetIntInVsockBuffer(uint8_t* buf, size_t index, int val) {
  val = htonl(val);
  std::memcpy(&buf[sizeof(int) * index], &val, sizeof(int));
  return;
}

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
    if (vsock.is_valid())
      break;
    if (vm_is_dead) {
      DLOG(INFO) << "ARCVM is already gone.";
      return true;
    }
    base::PlatformThread::Sleep(kArcPowerctlConnectDelay);
  }

  if (!vsock.is_valid())
    return false;

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

bool DoInflateBalloon(std::string socket_path,
                      uint64_t target,
                      base::TimeDelta timeout) {
  return CrosvmControl::Get()->SetBalloonSize(socket_path, target, timeout);
}

}  // namespace

SharedDataParam GetOemEtcSharedDataParam(uid_t euid, gid_t egid) {
  std::string oem_etc_uid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, euid);
  std::string oem_etc_gid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, egid);
  return SharedDataParam{.data_dir = base::FilePath(kOemEtcSharedDir),
                         .tag = kOemEtcSharedDirTag,
                         .uid_map = oem_etc_uid_map,
                         .gid_map = oem_etc_gid_map,
                         .enable_caches = SharedDataParam::Cache::kAlways};
}

ArcVm::ArcVm(Config config)
    : VmBaseImpl(VmBaseImpl::Config{
          .vsock_cid = config.vsock_cid,
          .network = std::move(config.network),
          .seneschal_server_proxy = std::move(config.seneschal_server_proxy),
          .cros_vm_socket = kCrosvmSocket,
          .runtime_dir = std::move(config.runtime_dir),
      }),
      data_disk_path_(config.data_disk_path),
      features_(config.features),
      balloon_refresh_time_(base::Time::Now() + kBalloonRefreshTime),
      vmm_swap_metrics_(std::move(config.vmm_swap_metrics)),
      swap_policy_timer_(std::move(config.swap_policy_timer)),
      swap_state_monitor_timer_(std::move(config.swap_state_monitor_timer)),
      vmm_swap_low_disk_policy_(std::move(config.vmm_swap_low_disk_policy)),
      vmm_swap_tbw_policy_(config.vmm_swap_tbw_policy),
      vmm_swap_usage_policy_(config.vmm_swap_usage_path),
      vm_swapping_notify_callback_(
          std::move(config.vm_swapping_notify_callback)),
      guest_memory_size_(config.guest_memory_size),
      virtio_blk_metrics_(std::move(config.virtio_blk_metrics)),
      balloon_metrics_(std::move(config.balloon_metrics)),
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

  if (!vm->balloon_request_thread_.Start()) {
    LOG(ERROR) << "Failed to start balloon stats thread";
    return {};
  }

  // Set up LMKD VSOCK listener if VmMemoryManagementClient is disabled.
  if (!vm->features_.use_vm_memory_management_client && !vm->SetupLmkdVsock()) {
    LOG(ERROR) << "Failed to initialize LMKD VSOCK connection.";
    return {};
  }

  if (!vm->Start(std::move(kernel), std::move(vm_builder))) {
    return {};
  }

  return vm;
}

bool ArcVm::Start(base::FilePath kernel, VmBuilder vm_builder) {
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
    vm_builder.EnableVideoDecoder(true /* enable */);
    vm_builder.EnableVideoEncoder(true /* enable */);
  }

  const base::FilePath jemalloc_config_file(kJemallocConfigFile);

  // Create a config symlink for memory-rich devices.
  int64_t sys_memory_mb = base::SysInfo::AmountOfPhysicalMemoryMB();

  // jemalloc_config_file might have been created on the
  // previous ARCVM boot. If the file already exists we do nothing.
  if ((sys_memory_mb >= kDefaultHighMemDeviceThreshold ||
       features_.low_mem_jemalloc_arenas_enabled) &&
      !base::IsLink(jemalloc_config_file)) {
    const base::FilePath jemalloc_setting(kJemallocHighMemDeviceConfig);
    // This symbolic link does not point to any file. It is used as a string
    // which contains the allocator config.
    if (!base::CreateSymbolicLink(jemalloc_setting, jemalloc_config_file)) {
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
      .SetSyslogTag(base::StringPrintf("ARCVM(%u)", vsock_cid_))
      .EnableGpu(true /* enable */)
      .AppendAudioDevice(
          "capture=true,backend=cras,client_type=arcvm,"
          "socket_type=unified,num_input_devices=3,"
          "num_output_devices=4,"
          "output_device_config=[[],[],[],[stream_type=pro_audio]],"
          "input_device_config=[[],[],[stream_type=pro_audio]]")
      // Each shared directory is a new PCI device, before adding a new shared
      // directory configuration, please consult if you really do need to add a
      // new PCI device. TODO(b/237618542): Unify these.
      .AppendSharedDir(GetOemEtcSharedDataParam(geteuid(), getegid()))
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kTestHarnessSharedDir),
                          .tag = kTestHarnessSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kApkCacheSharedDir),
                          .tag = kApkCacheSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(CreateFontsSharedDataParam())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kLibSharedDir),
                          .tag = kLibSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kUsrLibSharedDir),
                          .tag = kUsrLibSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kSbinSharedDir),
                          .tag = kSbinSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kUsrBinSharedDir),
                          .tag = kUsrBinSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .AppendSharedDir(
          SharedDataParam{.data_dir = jemalloc_config_file.DirName(),
                          .tag = kJemallocSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = SharedDataParam::Cache::kAlways,
                          .ascii_casefold = false,
                          .posix_acl = true})
      .EnableBattery(true /* enable */)
      .EnableDelayRt(true /* enable */);

  if (USE_CROSVM_VULKAN) {
    vm_builder.EnableVulkan(true).EnableRenderServer(true);
  }

  if (USE_CROSVM_VIRTGPU_NATIVE_CONTEXT) {
    vm_builder.EnableVirtgpuNativeContext(true);
  }

  if (USE_CROSVM_CROSS_DOMAIN_CONTEXT) {
    vm_builder.EnableCrossDomainContext(true);
  }

  std::unique_ptr<CustomParametersForDev> custom_parameters =
      MaybeLoadCustomParametersForDev(apps::ARCVM, use_dev_conf());

  // Add /usr/local/bin as a shared directory which is located in a dev
  // partition.
  std::string channel_string;
  const bool is_test_image = base::SysInfo::GetLsbReleaseValue(
                                 "CHROMEOS_RELEASE_TRACK", &channel_string) &&
                             base::StartsWith(channel_string, "test");
  if (is_test_image) {
    const base::FilePath usr_local_bin_dir(kUsrLocalBinSharedDir);
    if (base::PathExists(usr_local_bin_dir)) {
      vm_builder
          .AppendSharedDir(
              SharedDataParam{.data_dir = usr_local_bin_dir,
                              .tag = kUsrLocalBinSharedDirTag,
                              .uid_map = kAndroidUidMap,
                              .gid_map = kAndroidGidMap,
                              .enable_caches = SharedDataParam::Cache::kAlways,
                              .ascii_casefold = false,
                              .posix_acl = true})
          .AppendSharedDir(
              SharedDataParam{.data_dir = base::FilePath(kUsrLocalLibSharedDir),
                              .tag = kUsrLocalLibSharedDirTag,
                              .uid_map = kAndroidUidMap,
                              .gid_map = kAndroidGidMap,
                              .enable_caches = SharedDataParam::Cache::kAlways,
                              .ascii_casefold = false,
                              .posix_acl = true});
    } else {
      // Powerwashing etc can delete the directory from test image device.
      // We shouldn't abort ARCVM boot even under such an environment.
      LOG(WARNING) << kUsrLocalBinSharedDir << " is missing on test image.";
    }
  }

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

bool ArcVm::AttachUsbDevice(uint8_t bus,
                            uint8_t addr,
                            uint16_t vid,
                            uint16_t pid,
                            int fd,
                            uint8_t* out_port) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, out_port);
}

bool ArcVm::DetachUsbDevice(uint8_t port) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port);
}

namespace {

std::optional<ZoneInfoStats> ArcVmZoneStats(uint32_t cid, bool log_on_error) {
  brillo::ProcessImpl vsh;
  vsh.AddArg("/usr/bin/vsh");
  vsh.AddArg(base::StringPrintf("--cid=%u", cid));
  vsh.AddArg("--user=root");
  vsh.AddArg("--");
  vsh.AddArg("cat");
  vsh.AddArg("/proc/zoneinfo");
  vsh.RedirectUsingMemory(STDOUT_FILENO);
  vsh.RedirectUsingMemory(STDERR_FILENO);

  if (vsh.Run() != 0) {
    if (log_on_error) {
      LOG(ERROR) << "Failed to run vsh: " << vsh.GetOutputString(STDERR_FILENO);
    }
    return std::nullopt;
  }

  std::string zoneinfo = vsh.GetOutputString(STDOUT_FILENO);
  return ParseZoneInfoStats(zoneinfo);
}

}  // namespace

void ArcVm::InitializeBalloonPolicy(const MemoryMargins& margins,
                                    const std::string& vm) {
  // When the VmMemoryManagementClient is used, there is no balloon
  // policy for ARCVM.
  if (features_.use_vm_memory_management_client) {
    return;
  }

  balloon_init_attempts_--;

  // Only log on error if this is our last attempt. We expect some failures
  // early in boot, so we shouldn't spam the log with them.
  auto guest_stats = ArcVmZoneStats(vsock_cid_, balloon_init_attempts_ == 0);
  auto host_lwm = HostZoneLowSum(balloon_init_attempts_ == 0);
  if (guest_stats && host_lwm) {
    balloon_policy_ = std::make_unique<LimitCacheBalloonPolicy>(
        margins, *host_lwm, *guest_stats, kArcVmLimitCachePolicyParams, vm,
        raw_ref<mm::BalloonMetrics>::from_ptr(balloon_metrics_.get()));
    return;
  } else if (balloon_init_attempts_ > 0) {
    // We still have attempts left. Leave balloon_policy_ uninitialized, and
    // we will try again next time.
    return;
  } else {
    LOG(ERROR) << "Failed to initialize LimitCacheBalloonPolicy, falling "
               << "back to BalanceAvailableBalloonPolicy";
  }

  // No balloon policy parameters, so fall back to older policy.
  // NB: we override the VmBaseImpl method to provide the 48 MiB bias.
  balloon_policy_ = std::make_unique<BalanceAvailableBalloonPolicy>(
      margins.critical, MiB(48), vm);
}

const std::unique_ptr<BalloonPolicyInterface>& ArcVm::GetBalloonPolicy(
    const MemoryMargins& margins, const std::string& vm) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // While it is enabling vmm-swap, balloon policy is suspended and the balloon
  // is kept as big as possible to keep guest memory minimized.
  if (aggressive_balloon_running_) {
    static const std::unique_ptr<BalloonPolicyInterface> null_balloon_policy;
    return null_balloon_policy;
  }
  if (balloon_refresh_time_ && base::Time::Now() > *balloon_refresh_time_) {
    balloon_policy_.reset();
    balloon_refresh_time_.reset();
  }
  if (!balloon_policy_) {
    InitializeBalloonPolicy(margins, vm);
  }
  return balloon_policy_;
}

bool ArcVm::ListUsbDevice(std::vector<UsbDeviceEntry>* devices) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), devices);
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

  int64_t current_size = -1;
  if (!base::GetFileSize(data_disk_path_, &current_size)) {
    *failure_reason = "Unable to get current disk size";
    LOG(ERROR) << "ArcVm::ResizeDisk failed: " << *failure_reason;
    return DiskImageStatus::DISK_STATUS_FAILED;
  }

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
  SwapVmResponse response;
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

void ArcVm::InflateAggressiveBalloon(AggressiveBalloonCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (aggressive_balloon_callback_) {
    LOG(WARNING) << "Aggressive balloon is already ongoing";
    RunFailureAggressiveBalloonCallback(
        std::move(callback), "aggressive balloon is already ongoing");
    return;
  }
  aggressive_balloon_callback_ = std::move(callback);
  balloon_request_thread_.task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&vm_tools::concierge::GetBalloonStats, GetVmSocketPath(),
                     std::nullopt),
      base::BindOnce(&ArcVm::StartAggressiveBalloonInflation,
                     weak_ptr_factory_.GetWeakPtr()));
}

void ArcVm::StartAggressiveBalloonInflation(
    std::optional<BalloonStats> stats_opt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!aggressive_balloon_callback_) {
    return;
  }
  if (!stats_opt) {
    LOG(ERROR) << "Failed to get latest balloon stats";
    RunFailureAggressiveBalloonCallback(std::move(aggressive_balloon_callback_),
                                        "failed to get latest balloon stats");
    return;
  }
  LOG(INFO) << "Inflating aggressive balloon";
  aggressive_balloon_target_ = stats_opt->balloon_actual;
  aggressive_balloon_running_ = true;
  aggressive_balloon_interval_ = kInitAggressiveBalloonInterval;
  DoAggressiveBalloonStep();
}

void ArcVm::DoAggressiveBalloonStep() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!aggressive_balloon_running_)
    return;

  aggressive_balloon_target_ += kAggressiveBalloonIncrementSize;

  balloon_request_thread_.task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DoInflateBalloon, GetVmSocketPath(),
                     aggressive_balloon_target_, kAggressiveBalloonPuffTimeout),
      base::BindOnce(&ArcVm::OnAggressiveBalloonStepDone,
                     weak_ptr_factory_.GetWeakPtr()));
}

void ArcVm::OnAggressiveBalloonStepDone(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!aggressive_balloon_running_)
    return;

  if (success) {
    if (balloon_policy_) {
      balloon_policy_->UpdateCurrentBalloonSize(aggressive_balloon_target_);
    }

    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ArcVm::DoAggressiveBalloonStep,
                       weak_ptr_factory_.GetWeakPtr()),
        aggressive_balloon_interval_);
    return;
  }

  if (aggressive_balloon_interval_ >= kMaxAggressiveBalloonInterval) {
    LOG(INFO)
        << "Aggressive balloon timed out, assuming guest memory is shrunk.";
    aggressive_balloon_running_ = false;
    if (aggressive_balloon_callback_) {
      AggressiveBalloonResponse response;
      response.set_success(true);
      std::move(aggressive_balloon_callback_).Run(response);
    }
    return;
  }

  aggressive_balloon_target_ =
      aggressive_balloon_target_ > kAggressiveBalloonBackoffBytes
          ? aggressive_balloon_target_ - kAggressiveBalloonBackoffBytes
          : 0;
  // We didn't get an LMKD kill request - likely due to userspace locking
  // up. Increase the puff interval to give LMKD more time to respond.
  aggressive_balloon_interval_ *= 2;
  // Immediately execute a step to apply the new reduced balloon target, since
  // a puff failure generally means the guest is OOM and is thus locked up.
  DoAggressiveBalloonStep();
}

void ArcVm::StopAggressiveBalloon(AggressiveBalloonResponse& response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Stop aggressive balloon";
  if (aggressive_balloon_callback_) {
    RunFailureAggressiveBalloonCallback(std::move(aggressive_balloon_callback_),
                                        "aggressive balloon is disabled");
  }
  aggressive_balloon_running_ = false;
  response.set_success(true);
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

bool ArcVm::SetupLmkdVsock() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  arcvm_lmkd_vsock_fd_.reset(socket(AF_VSOCK, SOCK_STREAM, 0));

  if (!arcvm_lmkd_vsock_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create ArcVM LMKD vsock";
    return false;
  }

  struct sockaddr_vm sa {};
  sa.svm_family = AF_VSOCK;
  sa.svm_cid = VMADDR_CID_ANY;
  sa.svm_port = kLmkdKillDecisionPort;

  if (bind(arcvm_lmkd_vsock_fd_.get(),
           reinterpret_cast<const struct sockaddr*>(&sa), sizeof(sa)) == -1) {
    PLOG(ERROR) << "Failed to bind arcvm LMKD VSOCK.";
    return false;
  }

  // Only one ARCVM instance at a time, so a backlog of 1 is sufficient.
  if (listen(arcvm_lmkd_vsock_fd_.get(), 1) == -1) {
    PLOG(ERROR)
        << "Failed to start listening for a connection on ArcVM LMKD VSOCK";
    return false;
  }

  // The watchers are destroyed at the same time as the ArcVm instance, so this
  // callback cannot be called after the ArcVm instance is destroyed. Therefore,
  // Unretained is safe to use here.
  lmkd_vsock_accept_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      arcvm_lmkd_vsock_fd_.get(),
      base::BindRepeating(&ArcVm::HandleLmkdVsockAccept,
                          base::Unretained(this)));
  if (!lmkd_vsock_accept_watcher_) {
    PLOG(ERROR) << "Failed to watch LMKD listening socket";
    return false;
  }

  LOG(INFO) << "Waiting for LMKD socket connections...";

  return true;
}

void ArcVm::HandleLmkdVsockAccept() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  lmkd_client_fd_.reset(
      HANDLE_EINTR(accept(arcvm_lmkd_vsock_fd_.get(), nullptr, nullptr)));
  if (!lmkd_client_fd_.is_valid()) {
    PLOG(ERROR) << "LMKD failed to accept";
    return;
  }

  // Don't listen for accepts anymore since we have a client
  lmkd_vsock_accept_watcher_.reset();

  LOG(INFO) << "Concierge accepted connection from LMKD";

  // The watchers are destroyed at the same time as the ArcVm instance, so this
  // callback cannot be called after the ArcVm instance is destroyed. Therefore,
  // Unretained is safe to use here.
  lmkd_vsock_read_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      lmkd_client_fd_.get(),
      base::BindRepeating(&ArcVm::HandleLmkdVsockRead, base::Unretained(this)));

  if (!lmkd_vsock_read_watcher_) {
    PLOG(ERROR) << "Failed to start watching LMKD Vsock for reads";
    lmkd_vsock_read_watcher_.reset();
    return;
  }
}

uint64_t ArcVm::DeflateBalloonOnLmkd(int oom_score_adj, uint64_t proc_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint64_t freed_space = 0;
  if (aggressive_balloon_running_) {
    if (oom_score_adj <= kPlatformPerceptibleMaxOmmScoreAdjValue) {
      // Load the latest actual balloon size. LMKD may notify multiple process
      // in a very short period and balloon size target can be not reflected to
      // the actual balloon size.
      auto stats_opt = GetBalloonStats(base::Milliseconds(500));
      uint64_t balloon_actual;
      if (stats_opt) {
        balloon_actual = stats_opt->balloon_actual;
      } else {
        LOG(WARNING) << "Failed to query balloon stats, using target as actual";
        // Use the latest aggressive_balloon_target_ - last incremented size as
        // the pessimistic balloon actual size.
        balloon_actual =
            aggressive_balloon_target_ > kAggressiveBalloonIncrementSize
                ? aggressive_balloon_target_ - kAggressiveBalloonIncrementSize
                : 0;
      }
      uint64_t balloon_target =
          proc_size > balloon_actual ? 0 : balloon_actual - proc_size;
      freed_space = balloon_actual - balloon_target;
      LOG(INFO) << "Deflated VirtIO balloon to save process (OOM Score: "
                << oom_score_adj << ", Size: " << proc_size << ") Balloon: ("
                << balloon_actual << ") -> (" << balloon_target << ")";
      SetBalloonSize(balloon_target);
      aggressive_balloon_running_ = false;
      if (aggressive_balloon_callback_) {
        AggressiveBalloonResponse response;
        response.set_success(true);
        std::move(aggressive_balloon_callback_).Run(response);
      }
    }
    return freed_space;
  }

  uint64_t new_balloon_size = 0;
  if (balloon_policy_ &&
      balloon_policy_->DeflateBalloonToSaveProcess(
          proc_size, oom_score_adj, new_balloon_size, freed_space)) {
    SetBalloonSize(new_balloon_size);
  }
  return freed_space;
}

void ArcVm::HandleLmkdVsockRead() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(210075795) switch to using an int array for simplicity
  uint8_t lmkd_read_buf[kLmkdPacketMaxSize];

  if (!base::ReadFromFD(lmkd_client_fd_.get(),
                        reinterpret_cast<char*>(lmkd_read_buf),
                        kLmkdKillDecisionRequestPacketSize)) {
    // On failure (except for EAGAIN), disconnect the socket and wait for new
    // connection.
    if (errno != EAGAIN) {
      lmkd_vsock_read_watcher_.reset();
      lmkd_client_fd_.reset();

      // The watchers are destroyed at the same time as the ArcVm instance, so
      // this callback cannot be called after the ArcVm instance is destroyed.
      // Therefore, Unretained is safe to use here.
      lmkd_vsock_accept_watcher_ = base::FileDescriptorWatcher::WatchReadable(
          arcvm_lmkd_vsock_fd_.get(),
          base::BindRepeating(&ArcVm::HandleLmkdVsockAccept,
                              base::Unretained(this)));
      if (!lmkd_vsock_accept_watcher_) {
        PLOG(ERROR) << "Failed to restart watching LMKD Vsock";
      }
    } else {
      PLOG(ERROR) << "Failed to read from LMKD Vsock connection.";
    }

    return;
  }

  int cmd_id = GetIntFromVsockBuffer(lmkd_read_buf, 0);
  int sequence_num = GetIntFromVsockBuffer(lmkd_read_buf, 1);
  int proc_size_kb = GetIntFromVsockBuffer(lmkd_read_buf, 2);
  int oom_score_adj = GetIntFromVsockBuffer(lmkd_read_buf, 3);

  if (cmd_id != kLmkProcKillCandidate) {
    LOG(ERROR) << "Unknown command received from LMKD: " << cmd_id;
    return;
  }

  // Proc size comes from LMKD in KB units
  uint64_t freed_space = DeflateBalloonOnLmkd(oom_score_adj, KiB(proc_size_kb));

  // LMKD expects a response in KB units
  int freed_space_kb = freed_space / KiB(1);

  // TODO(210075795) switch to using an int array for simplicity
  uint8_t lmkd_reply_buf[kLmkdPacketMaxSize];

  SetIntInVsockBuffer(lmkd_reply_buf, 0, kLmkProcKillCandidate);
  SetIntInVsockBuffer(lmkd_reply_buf, 1, sequence_num);
  SetIntInVsockBuffer(lmkd_reply_buf, 2, freed_space_kb);

  if (!base::WriteFileDescriptor(
          lmkd_client_fd_.get(),
          {lmkd_reply_buf, kLmkdKillDecisionReplyPacketSize})) {
    PLOG(ERROR) << "Failed to write to LMKD VSOCK";
  }
}

base::TimeDelta ArcVm::CalculateVmmSwapDurationTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  double tbw_target_per_day =
      static_cast<double>(vmm_swap_tbw_policy_->GetTargetTbwPerDay());
  if (tbw_target_per_day <= 0) {
    return base::Days(28);
  }
  double factor = static_cast<double>(guest_memory_size_) / tbw_target_per_day;
  if (factor < 0) {
    LOG(ERROR) << "VmmSwapDurationTarget is negative: factor:" << factor;
    return base::TimeDelta::FiniteMax();
  } else if (factor > 28) {
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
    SwapVmResponse response;
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
        guest_memory_size_, base::BindOnce(&ArcVm::OnVmmSwapLowDiskPolicyResult,
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

  SwapVmResponse response;
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

void ArcVm::HandleSwapVmForceEnableRequest(SwapVmResponse& response) {
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

void ArcVm::HandleSwapVmDisableRequest(SwapVmResponse& response) {
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
    SwapVmResponse response;
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // TODO(b/287128076): Remove deprecated use of guest_zram_size.
  int64_t zram_size = request.guest_zram_mib() != 0
                          ? MiB(request.guest_zram_mib())
                          : request.guest_zram_size();
#pragma clang diagnostic pop

  std::vector<std::string> params = {
      "root=/dev/vda",
      "init=/init",
      // Note: Do not change the value "bertha". This string is checked in
      // platform2/metrics/process_meter.cc to detect ARCVM's crosvm processes,
      // for example.
      "androidboot.hardware=bertha",
      "androidboot.container=1",
      base::StringPrintf("androidboot.host_is_in_vm=%d", is_host_on_vm),
      base::StringPrintf("androidboot.dev_mode=%d", is_dev_mode),
      base::StringPrintf("androidboot.disable_runas=%d", !is_dev_mode),
      "androidboot.chromeos_channel=" + channel,
      base::StringPrintf("androidboot.seneschal_server_port=%d",
                         seneschal_server_port),
      base::StringPrintf("androidboot.iioservice_present=%d", USE_IIOSERVICE),
      base::StringPrintf("androidboot.arc_custom_tabs=%d",
                         mini_instance_request.arc_custom_tabs_experiment()),
      base::StringPrintf("androidboot.arc_file_picker=%d",
                         mini_instance_request.arc_file_picker_experiment()),
      base::StringPrintf("androidboot.enable_notifications_refresh=%d",
                         mini_instance_request.enable_notifications_refresh()),
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
      base::StringPrintf("androidboot.keyboard_shortcut_helper_integration=%d",
                         request.enable_keyboard_shortcut_helper_integration()),
      base::StringPrintf("androidboot.arcvm_virtio_blk_data=%d",
                         request.enable_virtio_blk_data()),
      base::StringPrintf("androidboot.zram_size=%" PRId64, zram_size),
      base::StringPrintf("androidboot.arc_switch_to_keymint=%d",
                         mini_instance_request.arc_switch_to_keymint()),
      // Avoid the RCU synchronization from blocking. See b/285791678#comment74
      // for the context.
      "rcupdate.rcu_expedited=1",
      "rcutree.kthread_prio=1",
  };

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

  if (request.enable_web_view_zygote_lazy_init())
    params.push_back("androidboot.arc.web_view_zygote.lazy_init=1");
  if (request.rootfs_writable())
    params.push_back("rw");

  auto guest_swappiness = request.guest_swappiness();
  if (guest_swappiness > 0) {
    params.push_back(
        base::StringPrintf("sysctl.vm.swappiness=%d", guest_swappiness));
  }

  // We run vshd under a restricted domain on non-test images.
  // (go/arcvm-android-sh-restricted)
  if (channel == "testimage")
    params.push_back("androidboot.vshd_service_override=vshd_for_test");
  if (request.enable_broadcast_anr_prenotify())
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

  if (mini_instance_request.arc_generate_pai())
    params.push_back("androidboot.arc_generate_pai=1");
  if (mini_instance_request.disable_download_provider())
    params.push_back("androidboot.disable_download_provider=1");
  // Only add boot property if flag to disable media store maintenance is set.
  if (mini_instance_request.disable_media_store_maintenance()) {
    params.push_back("androidboot.disable_media_store_maintenance=1");
    LOG(INFO) << "MediaStore maintenance task(s) are disabled";
  }
  if (mini_instance_request.enable_tts_caching())
    params.push_back("androidboot.arc.tts.caching=1");

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

}  // namespace vm_tools::concierge
