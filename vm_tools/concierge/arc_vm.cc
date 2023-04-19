// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/arc_vm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <csignal>
#include <cstring>
#include <optional>
#include <tuple>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <chromeos/constants/vm_tools.h>
#include <chromeos/patchpanel/net_util.h>
#include <vboot/crossystem.h>

#include "vm_tools/concierge/tap_device_builder.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {
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

// Path to the development configuration file (only visible in dev mode).
constexpr char kDevConfFilePath[] = "/usr/local/vms/etc/arcvm_dev.conf";

// Custom parameter key to override the kernel path
constexpr char kKeyToOverrideKernelPath[] = "KERNEL_PATH";

// Custom parameter key to override the o_direct= disk parameter.
constexpr char kKeyToOverrideODirect[] = "O_DIRECT";

// Custom parameter key to override the multiple_workers= disk parameter.
constexpr char kKeyToOverrideBlockMultipleWorkers[] = "BLOCK_MULTIPLE_WORKERS";

// Custom parameter key to override the async executor for the disk devices.
constexpr char kKeyToOverrideIoBlockAsyncExecutor[] = "BLOCK_ASYNC_EXECUTOR";

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

// The default initialization parameters for ARCVM's LimitCacheBalloonPolicy
static constexpr LimitCacheBalloonPolicy::Params kArcVmLimitCachePolicyParams =
    {
        .reclaim_target_cache = 322560 * KIB,
        .critical_target_cache = 322560 * KIB,
        .moderate_target_cache = 0,
        .responsive_max_deflate_bytes = 256 * MIB,
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

}  // namespace

std::string GetOemEtcSharedDataParam(uid_t euid, gid_t egid) {
  std::string oem_etc_uid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, euid);
  std::string oem_etc_gid_map =
      base::StringPrintf(kOemEtcUgidMapTemplate, egid);
  return SharedDataParam{.data_dir = base::FilePath(kOemEtcSharedDir),
                         .tag = kOemEtcSharedDirTag,
                         .uid_map = oem_etc_uid_map,
                         .gid_map = oem_etc_gid_map,
                         .enable_caches = true}
      .to_string();
}

ArcVm::ArcVm(int32_t vsock_cid,
             std::unique_ptr<patchpanel::Client> network_client,
             std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
             base::FilePath runtime_dir,
             base::FilePath data_disk_path,
             VmMemoryId vm_memory_id,
             ArcVmFeatures features)
    : VmBaseImpl(std::move(network_client),
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 kCrosvmSocket,
                 std::move(runtime_dir),
                 vm_memory_id),
      data_disk_path_(data_disk_path),
      features_(features),
      balloon_refresh_time_(base::Time::Now() + kBalloonRefreshTime) {}

ArcVm::~ArcVm() {
  Shutdown();
}

std::unique_ptr<ArcVm> ArcVm::Create(
    base::FilePath kernel,
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    base::FilePath data_image_path,
    VmMemoryId vm_memory_id,
    ArcVmFeatures features,
    VmBuilder vm_builder) {
  auto vm = std::unique_ptr<ArcVm>(
      new ArcVm(vsock_cid, std::move(network_client),
                std::move(seneschal_server_proxy), std::move(runtime_dir),
                std::move(data_image_path), vm_memory_id, features));

  if (!vm->SetupLmkdVsock()) {
    vm.reset();
    return vm;
  }

  if (!vm->Start(std::move(kernel), std::move(vm_builder))) {
    vm.reset();
  }

  return vm;
}

bool ArcVm::Start(base::FilePath kernel, VmBuilder vm_builder) {
  // Get the available network interfaces.
  network_devices_ = network_client_->NotifyArcVmStartup(vsock_cid_);
  if (network_devices_.empty()) {
    LOG(ERROR) << "No network devices available";
    return false;
  }

  // Open the tap device(s).
  bool no_tap_fd_added = true;
  for (const auto& dev : network_devices_) {
    auto fd =
        OpenTapDevice(dev.ifname, true /*vnet_hdr*/, nullptr /*ifname_out*/);
    if (!fd.is_valid()) {
      LOG(ERROR) << "Unable to open and configure TAP device " << dev.ifname;
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
          VmBuilder::AudioDeviceType::kVirtio,
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
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kApkCacheSharedDir),
                          .tag = kApkCacheSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(CreateFontsSharedDataParam())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kLibSharedDir),
                          .tag = kLibSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kUsrLibSharedDir),
                          .tag = kUsrLibSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kSbinSharedDir),
                          .tag = kSbinSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(
          SharedDataParam{.data_dir = base::FilePath(kUsrBinSharedDir),
                          .tag = kUsrBinSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
      .AppendSharedDir(
          SharedDataParam{.data_dir = jemalloc_config_file.DirName(),
                          .tag = kJemallocSharedDirTag,
                          .uid_map = kAndroidUidMap,
                          .gid_map = kAndroidGidMap,
                          .enable_caches = true,
                          .ascii_casefold = false,
                          .posix_acl = true}
              .to_string())
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

  CustomParametersForDev custom_parameters;

  const bool is_dev_mode = (VbGetSystemPropertyInt("cros_debug") == 1);
  // Load any custom parameters from the development configuration file if the
  // feature is turned on (default) and path exists (dev mode only).
  if (is_dev_mode && use_dev_conf()) {
    const base::FilePath dev_conf(kDevConfFilePath);
    if (base::PathExists(dev_conf)) {
      std::string data;
      if (!base::ReadFileToString(dev_conf, &data)) {
        PLOG(ERROR) << "Failed to read file " << dev_conf.value();
        return false;
      }
      custom_parameters = CustomParametersForDev(data);
    }
  }

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
          .AppendSharedDir(SharedDataParam{.data_dir = usr_local_bin_dir,
                                           .tag = kUsrLocalBinSharedDirTag,
                                           .uid_map = kAndroidUidMap,
                                           .gid_map = kAndroidGidMap,
                                           .enable_caches = true,
                                           .ascii_casefold = false,
                                           .posix_acl = true}
                               .to_string())
          .AppendSharedDir(
              SharedDataParam{.data_dir = base::FilePath(kUsrLocalLibSharedDir),
                              .tag = kUsrLocalLibSharedDirTag,
                              .uid_map = kAndroidUidMap,
                              .gid_map = kAndroidGidMap,
                              .enable_caches = true,
                              .ascii_casefold = false,
                              .posix_acl = true}
                  .to_string());
    } else {
      // Powerwashing etc can delete the directory from test image device.
      // We shouldn't abort ARCVM boot even under such an environment.
      LOG(WARNING) << kUsrLocalBinSharedDir << " is missing on test image.";
    }
  }

  if (custom_parameters.ObtainSpecialParameter(kKeyToOverrideODirect)
          .value_or("false") == "true") {
    vm_builder.EnableODirect(true);
    /* block size for DM-verity root file system */
    vm_builder.SetBlockSize(4096);
  }

  if (custom_parameters
          .ObtainSpecialParameter(kKeyToOverrideBlockMultipleWorkers)
          .value_or("false") == "true") {
    vm_builder.EnableMultipleWorkers(true);
  }

  const auto block_async_executor = custom_parameters.ObtainSpecialParameter(
      kKeyToOverrideIoBlockAsyncExecutor);
  if (block_async_executor) {
    const auto executor_enum =
        StringToAsyncExecutor(block_async_executor.value());
    if (!executor_enum.has_value()) {
      LOG(ERROR) << "Unknown value for BLOCK_ASYNC_EXECUTOR: "
                 << block_async_executor.value();
      return false;
    }
    vm_builder.SetBlockAsyncExecutor(executor_enum.value());
  }

  // Finally set the path to the kernel.
  const std::string kernel_path =
      custom_parameters.ObtainSpecialParameter(kKeyToOverrideKernelPath)
          .value_or(kernel.value());
  vm_builder.SetKernel(base::FilePath(kernel_path));

  auto args = vm_builder.BuildVmArgs(&custom_parameters);

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for ARCVM's crosvm processes. Note that once crosvm starts,
  // crosvm adds its vCPU threads to the kArcvmVcpuCpuCgroup by itself.
  process_.SetPreExecCallback(base::BindOnce(
      &SetUpCrosvmProcess, base::FilePath(kArcvmCpuCgroup).Append("tasks")));

  if (!StartProcess(std::move(args))) {
    LOG(ERROR) << "Failed to start VM process";
    // Release any network resources.
    network_client_->NotifyArcVmShutdown(vsock_cid_);
    return false;
  }

  return true;
}

bool ArcVm::Shutdown() {
  // Notify arc-patchpanel that ARCVM is down.
  // This should run before the process existence check below since we still
  // want to release the network resources on crash.
  if (!network_client_->NotifyArcVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Unable to notify networking services";
  }

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
  balloon_init_attempts_--;

  // Only log on error if this is our last attempt. We expect some failures
  // early in boot, so we shouldn't spam the log with them.
  auto guest_stats = ArcVmZoneStats(vsock_cid_, balloon_init_attempts_ == 0);
  auto host_lwm = HostZoneLowSum(balloon_init_attempts_ == 0);
  if (guest_stats && host_lwm) {
    balloon_policy_ = std::make_unique<LimitCacheBalloonPolicy>(
        margins, *host_lwm, *guest_stats, kArcVmLimitCachePolicyParams, vm);
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
      margins.critical, 48 * MIB, vm);
}

const std::unique_ptr<BalloonPolicyInterface>& ArcVm::GetBalloonPolicy(
    const MemoryMargins& margins, const std::string& vm) {
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
  Suspend();
}

void ArcVm::HandleSuspendDone() {
  Resume();
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
  for (const auto& dev : network_devices_) {
    if (dev.ifname != "arc0") {
      continue;
    }
    if (dev.ipv4_addr.size() != 4) {
      return 0;
    }
    return patchpanel::Ipv4Addr(dev.ipv4_addr[0], dev.ipv4_addr[1],
                                dev.ipv4_addr[2], dev.ipv4_addr[3]);
  }
  return 0;
}

VmBaseImpl::Info ArcVm::GetInfo() {
  VmBaseImpl::Info info = {
      .ipv4_address = IPv4Address(),
      .pid = pid(),
      .cid = cid(),
      .vm_memory_id = vm_memory_id_,
      .seneschal_server_handle = seneschal_server_handle(),
      .status = VmBaseImpl::Status::RUNNING,
      .type = VmInfo::ARC_VM,
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

bool ArcVm::SetupLmkdVsock() {
  DCHECK(sequence_checker_.CalledOnValidSequence());

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
  DCHECK(sequence_checker_.CalledOnValidSequence());
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

void ArcVm::HandleLmkdVsockRead() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
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
  uint64_t proc_size = proc_size_kb * KIB;
  uint64_t new_balloon_size = 0;
  uint64_t freed_space = 0;

  if (balloon_policy_ &&
      balloon_policy_->DeflateBalloonToSaveProcess(
          proc_size, oom_score_adj, new_balloon_size, freed_space)) {
    SetBalloonSize(new_balloon_size);
  }

  // LMKD expects a response in KB units
  int freed_space_kb = freed_space / KIB;

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
      "androidboot.enable_consumer_auto_update_toggle=" +
          std::to_string(request.enable_consumer_auto_update_toggle()),
      base::StringPrintf("androidboot.update_o4c_list_via_a2c2=%d",
                         request.update_o4c_list_via_a2c2()),
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
      base::StringPrintf("androidboot.zram_size=%d", request.guest_zram_size()),
      base::StringPrintf("androidboot.arc_switch_to_keymint=%d",
                         mini_instance_request.arc_switch_to_keymint()),
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
  LOG(INFO) << base::StringPrintf("Setting ARCVM guest's zram size to %d",
                                  request.guest_zram_size());

  if (request.enable_web_view_zygote_lazy_init())
    params.push_back("androidboot.arc.web_view_zygote.lazy_init=1");
  if (request.enable_rw())
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

  return params;
}

}  // namespace concierge
}  // namespace vm_tools
