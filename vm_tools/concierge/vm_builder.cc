// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <re2/re2.h>

#include "vm_tools/concierge/pci_utils.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {
namespace {
// Path to the default wayland socket.
constexpr char kWaylandSocket[] = "/run/chrome/wayland-0";

constexpr char kVirglRenderServerPath[] = "/usr/libexec/virgl_render_server";

// Path to scudo allocator for GWP-ASan enablement in venus render server
#if defined(__x86_64__)
constexpr char kScudoPath[] =
    "/usr/lib64/libclang_rt.scudo_standalone-x86_64.so";
#elif defined(__aarch64__)
constexpr char kScudoPath[] =
    "/usr/lib64/libclang_rt.scudo_standalone-aarch64.so";
#elif defined(__arm__)
constexpr char kScudoPath[] = "/usr/lib/libclang_rt.scudo_standalone-armhf.so";
#else
static_assert(
    !USE_CROSVM_VENUS_GWP_ASAN,
    "scudo library *MUST* be available if concierge is built expecting it");
#endif

// Custom parameter key to override the o_direct= disk parameter for specific
// Nth entry.
constexpr char kKeyToOverrideODirectN[] = "O_DIRECT_N";

// Custom parameter key to override the multiple_workers= disk parameter.
constexpr char kKeyToOverrideBlockMultipleWorkers[] = "BLOCK_MULTIPLE_WORKERS";

// Custom parameter key to override the async executor for the disk devices.
constexpr char kKeyToOverrideIoBlockAsyncExecutor[] = "BLOCK_ASYNC_EXECUTOR";

// Custom parameter key to override the kernel path
constexpr char kKeyToOverrideKernelPath[] = "KERNEL_PATH";

constexpr char kUringAsyncExecutorString[] = "uring";
constexpr char kEpollAsyncExecutorString[] = "epoll";

std::string BooleanParameter(const char* parameter, bool value) {
  std::string result = base::StrCat({parameter, value ? "true" : "false"});
  return result;
}

std::string AsyncExecutorToString(VmBuilder::AsyncExecutor async_executor) {
  switch (async_executor) {
    case VmBuilder::AsyncExecutor::kUring:
      return kUringAsyncExecutorString;
    case VmBuilder::AsyncExecutor::kEpoll:
      return kEpollAsyncExecutorString;
  }
}

}  // namespace

// Convert a string to the corresponding AsyncExecutor. This returns nullopt if
// the given string is unknown.
std::optional<VmBuilder::AsyncExecutor> StringToAsyncExecutor(
    std::string_view async_executor) {
  if (async_executor == kUringAsyncExecutorString) {
    return std::optional{VmBuilder::AsyncExecutor::kUring};
  } else if (async_executor == kEpollAsyncExecutorString) {
    return std::optional{VmBuilder::AsyncExecutor::kEpoll};
  } else {
    LOG(ERROR) << "Failed to convert unknown string to AsyncExecutor"
               << async_executor;
    return std::nullopt;
  }
}

VmBuilder::VmBuilder() = default;

VmBuilder::VmBuilder(VmBuilder&&) = default;

VmBuilder& VmBuilder::operator=(VmBuilder&& other) = default;

VmBuilder::~VmBuilder() = default;

VmBuilder& VmBuilder::SetKernel(base::FilePath kernel) {
  kernel_ = std::move(kernel);
  return *this;
}

VmBuilder& VmBuilder::SetInitrd(base::FilePath initrd) {
  initrd_ = std::move(initrd);
  return *this;
}

VmBuilder& VmBuilder::SetBios(base::FilePath bios) {
  bios_ = std::move(bios);
  return *this;
}

VmBuilder& VmBuilder::SetPflash(base::FilePath pflash) {
  pflash_ = std::move(pflash);
  return *this;
}

VmBuilder& VmBuilder::SetRootfs(const struct Rootfs& rootfs) {
  rootfs_ = rootfs;
  return *this;
}

VmBuilder& VmBuilder::SetCpus(int32_t cpus) {
  cpus_ = cpus;
  return *this;
}

VmBuilder& VmBuilder::SetVmCpuArgs(const struct VmCpuArgs& vm_cpu_args) {
  vm_cpu_args_ = vm_cpu_args;
  return *this;
}

VmBuilder& VmBuilder::SetVsockCid(uint32_t vsock_cid) {
  vsock_cid_ = vsock_cid;
  return *this;
}

VmBuilder& VmBuilder::AppendDisk(VmBuilder::Disk disk) {
  disks_.push_back(std::move(disk));
  return *this;
}

VmBuilder& VmBuilder::AppendPmemDevice(VmBuilder::PmemDevice pmem_device) {
  pmem_devices_.push_back(std::move(pmem_device));
  return *this;
}

VmBuilder& VmBuilder::SetMemory(std::string_view memory_in_mb) {
  memory_in_mib_ = memory_in_mb;
  return *this;
}

VmBuilder& VmBuilder::SetBalloonBias(std::string_view balloon_bias_mib) {
  balloon_bias_mib_ = balloon_bias_mib;
  return *this;
}

VmBuilder& VmBuilder::EnableWorkingSetReporting(bool enable) {
  enable_working_set_reporting_ = enable;
  return *this;
}

VmBuilder& VmBuilder::SetSyslogTag(std::string_view syslog_tag) {
  syslog_tag_ = syslog_tag;
  return *this;
}

VmBuilder& VmBuilder::SetSocketPath(std::string_view socket_path) {
  vm_socket_path_ = socket_path;
  return *this;
}

VmBuilder& VmBuilder::AppendTapFd(base::ScopedFD fd) {
  tap_fds_.push_back(std::move(fd));
  return *this;
}

VmBuilder& VmBuilder::AppendKernelParam(std::string_view param) {
  kernel_params_.emplace_back(param);
  return *this;
}

VmBuilder& VmBuilder::AppendOemString(std::string_view string) {
  oem_strings_.emplace_back(string);
  return *this;
}

VmBuilder& VmBuilder::AppendAudioDevice(std::string_view params) {
  audio_devices_.push_back(AudioDevice{.params{params}});
  return *this;
}

VmBuilder& VmBuilder::AppendSerialDevice(std::string_view device) {
  serial_devices_.emplace_back(device);
  return *this;
}

VmBuilder& VmBuilder::SetWaylandSocket(std::string_view socket) {
  // The "true" socket, which is the visual one, must be set first.
  DCHECK(wayland_sockets_.empty());
  if (socket.empty()) {
    // We want the empty string to mean "use the default socket", since that is
    // the behaviour we want if the user does not set the wayland socket in the
    // VirtualMachineSpec proto.
    wayland_sockets_.emplace_back(kWaylandSocket);
  } else {
    wayland_sockets_.emplace_back(socket);
  }
  return *this;
}

VmBuilder& VmBuilder::AddExtraWaylandSocket(std::string_view socket) {
  // Additional sockets must only be added after the "true" socket, since the
  // first socket provided to the VM will always be interpreted as the visual
  // one.
  DCHECK(!wayland_sockets_.empty());
  wayland_sockets_.emplace_back(socket);
  return *this;
}

VmBuilder& VmBuilder::AppendSharedDir(SharedDataParam shared_dir) {
  shared_dirs_.push_back(std::move(shared_dir));
  return *this;
}

VmBuilder& VmBuilder::AppendVhostUserFsFrontend(VhostUserFsFrontParam param) {
  vhost_user_fs_fronts_.push_back(std::move(param));
  return *this;
}

VmBuilder& VmBuilder::AppendCustomParam(std::string_view key,
                                        std::string_view value) {
  custom_params_.emplace_back(key, value);
  return *this;
}

VmBuilder& VmBuilder::EnableGpu(bool enable) {
  enable_gpu_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableDGpuPassthrough(bool enable) {
  enable_dgpu_passthrough_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableVulkan(bool enable) {
  enable_vulkan_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableBigGl(bool enable) {
#if USE_BIG_GL
  enable_big_gl_ = enable;
#else
  LOG_IF(WARNING, enable) << "Big GL is not supported on this board";
  enable_big_gl_ = false;
#endif
  return *this;
}

VmBuilder& VmBuilder::EnableRenderServer(bool enable) {
  enable_render_server_ = enable;
  return *this;
}

VmBuilder& VmBuilder::SetGpuCachePath(base::FilePath gpu_cache_path) {
  gpu_cache_path_ = std::move(gpu_cache_path);
  return *this;
}

VmBuilder& VmBuilder::SetGpuCacheSize(std::string_view gpu_cache_size_str) {
  gpu_cache_size_str_ = gpu_cache_size_str;
  return *this;
}

VmBuilder& VmBuilder::SetRenderServerCachePath(
    base::FilePath render_server_cache_path) {
  render_server_cache_path_ = std::move(render_server_cache_path);
  return *this;
}

VmBuilder& VmBuilder::SetPrecompiledCachePath(
    base::FilePath precompiled_cache_path) {
  precompiled_cache_path_ = std::move(precompiled_cache_path);
  return *this;
}

VmBuilder& VmBuilder::SetFozDbListPath(base::FilePath foz_db_list_path) {
  foz_db_list_path_ = std::move(foz_db_list_path);
  return *this;
}

VmBuilder& VmBuilder::SetRenderServerCacheSize(
    std::string_view render_server_cache_size_str) {
  render_server_cache_size_str_ = render_server_cache_size_str;
  return *this;
}

VmBuilder& VmBuilder::EnableGpuContextTypeDefaults() {
  enabled_gpu_context_types_.reset();
  return *this;
}

VmBuilder& VmBuilder::EnableGpuContextTypeVirgl(bool enable) {
  enabled_gpu_context_types_.set(GpuContextType::GPU_CONTEXT_TYPE_VIRGL,
                                 enable);
  return *this;
}

VmBuilder& VmBuilder::EnableGpuContextTypeVenus(bool enable) {
  enabled_gpu_context_types_.set(GpuContextType::GPU_CONTEXT_TYPE_VENUS,
                                 enable);
  return *this;
}

VmBuilder& VmBuilder::EnableGpuContextTypeCrossDomain(bool enable) {
  enabled_gpu_context_types_.set(GpuContextType::GPU_CONTEXT_TYPE_CROSS_DOMAIN,
                                 enable);
  return *this;
}

VmBuilder& VmBuilder::EnableGpuContextTypeDrm(bool enable) {
  enabled_gpu_context_types_.set(GpuContextType::GPU_CONTEXT_TYPE_DRM, enable);
  return *this;
}

VmBuilder& VmBuilder::EnableVtpmProxy(bool enable) {
  enable_vtpm_proxy_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableVideoDecoder(bool enable) {
  enable_video_decoder_ = enable;
  return *this;
}

VmBuilder& VmBuilder::SetVideoDecoder(std::string_view video_decoder) {
  video_decoder_ = video_decoder;
  return *this;
}

VmBuilder& VmBuilder::EnableVideoEncoder(bool enable) {
  enable_video_encoder_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableBattery(bool enable) {
  enable_battery_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableSmt(bool enable) {
  enable_smt_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableDelayRt(bool enable) {
  enable_delay_rt_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnablePerVmCoreScheduling(bool enable) {
  enable_per_vm_core_scheduling_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnablePvClock(bool enable) {
  enable_pvclock_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableODirectN(int n, bool enable) {
  disks_.at(n).o_direct = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableMultipleWorkers(bool enable) {
  for (auto& d : disks_) {
    d.multiple_workers = enable;
  }
  return *this;
}

VmBuilder& VmBuilder::SetBlockAsyncExecutor(AsyncExecutor executor) {
  for (auto& d : disks_) {
    d.async_executor = executor;
  }
  return *this;
}

VmBuilder& VmBuilder::SetBlockSize(size_t block_size) {
  for (auto& d : disks_) {
    d.block_size = block_size;
  }
  return *this;
}

VmBuilder& VmBuilder::SetBlockSizeN(size_t n, size_t block_size) {
  disks_.at(n).block_size = block_size;
  return *this;
}

VmBuilder& VmBuilder::SetVmmSwapDir(base::FilePath vmm_swap_dir) {
  vmm_swap_dir_ = std::move(vmm_swap_dir);
  return *this;
}

bool VmBuilder::ProcessCustomParameters(
    const CustomParametersForDev& devparams) {
  for (auto value : devparams.ObtainSpecialParameters(kKeyToOverrideODirectN)) {
    int64_t n;
    if (!base::StringToInt64(value, &n)) {
      LOG(ERROR) << "Unknown value for " << kKeyToOverrideODirectN << ": "
                 << value;
      return false;
    }
    EnableODirectN(n, true);

    /* block size for DM-verity root file system */
    SetBlockSizeN(n, 4096);
  }

  if (devparams.ObtainSpecialParameter(kKeyToOverrideBlockMultipleWorkers)
          .value_or("false") == "true") {
    EnableMultipleWorkers(true);
  }

  const auto block_async_executor =
      devparams.ObtainSpecialParameter(kKeyToOverrideIoBlockAsyncExecutor);
  if (block_async_executor) {
    const auto executor_enum =
        StringToAsyncExecutor(block_async_executor.value());
    if (!executor_enum.has_value()) {
      LOG(ERROR) << "Unknown value for BLOCK_ASYNC_EXECUTOR: "
                 << block_async_executor.value();
      return false;
    }
    SetBlockAsyncExecutor(executor_enum.value());
  }

  if (auto kernel_override =
          devparams.ObtainSpecialParameter(kKeyToOverrideKernelPath);
      kernel_override) {
    kernel_ = base::FilePath(kernel_override.value());
  }
  return true;
}

std::optional<base::StringPairs> VmBuilder::BuildVmArgs(
    CustomParametersForDev* devparams) && {
  if (devparams) {
    if (!ProcessCustomParameters(*devparams)) {
      return std::nullopt;
    }
  }

  base::StringPairs post_run_args = BuildRunParams();

  // Early-return when BuildRunParams() failed.
  if (post_run_args.empty()) {
    return std::nullopt;
  }

  base::StringPairs pre_run_args = BuildPreRunParams();
  std::vector<std::string> pre_crosvm_args;

  if (devparams) {
    devparams->Apply(post_run_args);
    devparams->AppendPrerunParams(pre_run_args);
    pre_crosvm_args = devparams->ObtainPrecrosvmParams();
  }

  base::StringPairs args;
  for (const auto& arg : pre_crosvm_args) {
    args.emplace_back(arg, "");
  }
  args.emplace_back(kCrosvmBin, "");
  args.insert(args.end(), pre_run_args.begin(), pre_run_args.end());

  args.emplace_back("run", "");
  args.insert(args.end(), post_run_args.begin(), post_run_args.end());

  // Kernel should be at the end.
  if (!kernel_.empty()) {
    args.emplace_back(kernel_.value(), "");
  }

  return args;
}

base::StringPairs VmBuilder::BuildPreRunParams() const {
  base::StringPairs args;
  if (!syslog_tag_.empty()) {
    args.emplace_back("--syslog-tag", syslog_tag_);
  }
  return args;
}

base::StringPairs VmBuilder::BuildRunParams() const {
  base::StringPairs args = {{"--cpus", std::to_string(cpus_)}};

  if (!memory_in_mib_.empty()) {
    args.emplace_back("--mem", memory_in_mib_);
  }

  if (!balloon_bias_mib_.empty()) {
    args.emplace_back("--balloon-bias-mib", balloon_bias_mib_);
  }

  if (enable_working_set_reporting_) {
    args.emplace_back("--balloon-ws-reporting", "");
  }

  for (const auto& tap_fd : tap_fds_) {
    args.emplace_back(
        "--net", "packed-queue=true,tap-fd=" + std::to_string(tap_fd.get()));
  }

  if (vsock_cid_.has_value()) {
    args.emplace_back("--cid", std::to_string(vsock_cid_.value()));
  }

  if (!vm_socket_path_.empty()) {
    args.emplace_back("--socket", vm_socket_path_);
  }

  for (const auto& w : wayland_sockets_) {
    args.emplace_back("--wayland-sock", w);
  }

  for (const auto& s : serial_devices_) {
    args.emplace_back("--serial", s);
  }

  if (enable_smt_.has_value()) {
    if (!enable_smt_.value()) {
      args.emplace_back("--no-smt", "");
    }
  }

  if (enable_delay_rt_) {
    args.emplace_back("--delay-rt", "");
  }

  if (enable_per_vm_core_scheduling_) {
    args.emplace_back("--per-vm-core-scheduling", "");
  }

  if (kernel_params_.size() > 0) {
    args.emplace_back("--params", base::JoinString(kernel_params_, " "));
  }

  for (const auto& s : oem_strings_) {
    args.emplace_back("--oem-strings", s);
  }

  if (rootfs_.has_value()) {
    const auto& rootfs = rootfs_.value();
    std::string rootfs_arg = rootfs.path.value();
    rootfs_arg += BooleanParameter(",ro=", !rootfs.writable);
    rootfs_arg += BooleanParameter(",root=", true);

    if (rootfs.device.find("pmem") != std::string::npos) {
      // TODO(davidriley): Re-add rootflags=dax once guest kernel has fix for
      // b/169339326.
      args.emplace_back("--pmem", rootfs_arg);
    } else {
      args.emplace_back("--block", rootfs_arg);
    }
  }

  for (const auto& dev : audio_devices_) {
    args.emplace_back("--virtio-snd", dev.params);
  }

  for (const auto& d : disks_) {
    auto disk_args = d.GetCrosvmArgs();
    args.insert(std::end(args), std::begin(disk_args), std::end(disk_args));
  }

  for (const auto& pmem_device : pmem_devices_) {
    auto device_args = pmem_device.GetCrosvmArgs();
    args.insert(std::end(args), std::begin(device_args), std::end(device_args));
  }

  if (enable_gpu_) {
    std::string gpu_arg = "vulkan=";
    gpu_arg += enable_vulkan_ ? "true" : "false";

    // some of these are related to other options (e.g. use_vulkan_, big_gl_)
    // but we treat them as orthogonal, requiring callers to manage their
    // desired context types explicitly to keep a 1:1 mapping between VmBuilder
    // options and the VMM's options.
    if (enabled_gpu_context_types_.any()) {
      gpu_arg += ",context-types=";
      if (enabled_gpu_context_types_.test(GPU_CONTEXT_TYPE_VIRGL)) {
        gpu_arg += ":virgl:virgl2";
      }
      if (enabled_gpu_context_types_.test(GPU_CONTEXT_TYPE_VENUS)) {
        gpu_arg += ":venus";
      }
      if (enabled_gpu_context_types_.test(GPU_CONTEXT_TYPE_CROSS_DOMAIN)) {
        gpu_arg += ":cross-domain";
      }
      if (enabled_gpu_context_types_.test(GPU_CONTEXT_TYPE_DRM)) {
        gpu_arg += ":drm";
      }
    }
    if (enable_big_gl_) {
      gpu_arg += ",gles=false";
    }
    if (!gpu_cache_path_.empty()) {
      gpu_arg += ",cache-path=" + gpu_cache_path_.value();
    }
    if (!gpu_cache_size_str_.empty()) {
      gpu_arg += ",cache-size=" + gpu_cache_size_str_;
    }
    gpu_arg += ",fixed-blob-mapping=";
    if constexpr (USE_CROSVM_FIXED_BLOB_MAPPING) {
      gpu_arg += "true";
    } else {
      gpu_arg += "false";
    }
    args.emplace_back("--gpu", gpu_arg);

    if (enable_render_server_) {
      std::string render_server_arg = "path=";
      render_server_arg += kVirglRenderServerPath;
      if (!render_server_cache_path_.empty()) {
        render_server_arg += ",cache-path=" + render_server_cache_path_.value();
      }
      if (!render_server_cache_size_str_.empty()) {
        render_server_arg += ",cache-size=" + render_server_cache_size_str_;
      }
      if (!foz_db_list_path_.empty()) {
        render_server_arg += ",foz-db-list-path=" + foz_db_list_path_.value();
      }
      if (!precompiled_cache_path_.empty()) {
        render_server_arg +=
            ",precompiled-cache-path=" + precompiled_cache_path_.value();
      }
      if constexpr (USE_CROSVM_VENUS_GWP_ASAN) {
        render_server_arg += ",ld-preload-path=" + std::string(kScudoPath);
      }
      args.emplace_back("--gpu-render-server", render_server_arg);
    }
  }

  if (enable_dgpu_passthrough_) {
    std::vector<base::FilePath> dgpu_devices = GetPciDevicesList(
        pci_utils::PciDeviceType::PCI_DEVICE_TYPE_DGPU_PASSTHROUGH);

    for (const auto& dgpu_device : dgpu_devices) {
      std::string dgpu_pt_arg =
          base::StringPrintf("%s,iommu=viommu", dgpu_device.value().c_str());
      args.emplace_back("--vfio", std::move(dgpu_pt_arg));
    }

    // TODO(b/297293470): These options appear to break borealis'
    // suspend+resume. The below disables them if there are no dgpu devices but
    // they will probably still be broken with dgpu.
    if (!dgpu_devices.empty()) {
      args.emplace_back("--s2idle", "");
      args.emplace_back("--ac-adapter", "");
    }
  }

  if (enable_vtpm_proxy_) {
    args.emplace_back("--vtpm-proxy", "");
  }

  if (enable_video_decoder_) {
    args.emplace_back("--video-decoder", video_decoder_);
  }

  if (enable_video_encoder_) {
    args.emplace_back("--video-encoder", "libvda");
  }

  if (enable_battery_) {
    args.emplace_back("--battery", "type=goldfish");
  }

  if (enable_pvclock_) {
    args.emplace_back("--pvclock", "");
#if defined(__x86_64__)
    // Virtual suspend time injection (VSTI) is the legacy solution to fix
    // guest watchdog issues on x86_64; it conflicts with pvclock so disable
    // it.
    // VSTI was never implemented on AArch64 and hence AArch64 does not need
    // this.
    args.emplace_back("--params", "no-vsti-guest");
#endif
  }

  for (const auto& shared_dir : shared_dirs_) {
    args.emplace_back("--shared-dir", shared_dir.to_string());
  }

  for (const auto& vhost_user_fs_dir : vhost_user_fs_fronts_) {
    args.emplace_back("--vhost-user-fs", vhost_user_fs_dir.to_string());
  }

  for (const auto& custom_param : custom_params_) {
    args.emplace_back(custom_param.first, custom_param.second);
  }

  if (!initrd_.empty()) {
    args.emplace_back("-i", initrd_.value());
  }

  if (!bios_.empty()) {
    args.emplace_back("--bios", bios_.value());
  }

  if (!pflash_.empty()) {
    args.emplace_back("--pflash", "path=" + pflash_.value());
  }

  if (!vmm_swap_dir_.empty()) {
    args.emplace_back("--swap", vmm_swap_dir_.value());
  }

  if (vm_cpu_args_.has_value()) {
    if (!vm_cpu_args_->cpu_affinity.empty()) {
      args.emplace_back("--cpu-affinity", vm_cpu_args_->cpu_affinity);
    }

    if (!vm_cpu_args_->cpu_capacity.empty()) {
      args.emplace_back("--cpu-capacity",
                        base::JoinString(vm_cpu_args_->cpu_capacity, ","));
    }

    for (const auto& cluster : vm_cpu_args_->cpu_clusters) {
      if (!cluster.empty()) {
        auto cpu_list = base::JoinString(cluster, ",");
        args.emplace_back("--cpu-cluster", cpu_list);
      }
    }
  }

  return args;
}

base::StringPairs VmBuilder::Disk::GetCrosvmArgs() const {
  std::string first = "--block";

  std::string readonly_arg = BooleanParameter(",ro=", !writable);

  std::string sparse_arg;
  if (sparse) {
    sparse_arg = BooleanParameter(",sparse=", sparse.value());
  }
  std::string o_direct_arg;
  if (o_direct) {
    o_direct_arg = BooleanParameter(",o_direct=", o_direct.value());
  }
  std::string multiple_workers_arg;
  if (multiple_workers) {
    multiple_workers_arg =
        BooleanParameter(",multiple-workers=", multiple_workers.value());
  }
  std::string block_size_arg;
  if (block_size) {
    block_size_arg = ",block_size=" + std::to_string(block_size.value());
  }
  std::string async_executor_arg;
  if (async_executor) {
    async_executor_arg = base::StrCat(
        {",async_executor=", AsyncExecutorToString(async_executor.value())});
  }
  std::string block_id_arg;
  if (block_id) {
    // Virtio_blk can't handle more than 20 chars:
    // https://docs.oasis-open.org/virtio/virtio/v1.2/csd01/virtio-v1.2-csd01.html#x1-2850006
    CHECK_LE(block_id.value().size(), 20);
    block_id_arg = base::StrCat({",id=", block_id.value()});
  }

  std::string second = base::StrCat(
      {path.value(), readonly_arg, sparse_arg, o_direct_arg,
       multiple_workers_arg, block_size_arg, async_executor_arg, block_id_arg});
  base::StringPairs result = {{std::move(first), std::move(second)}};
  return result;
}

base::StringPairs VmBuilder::PmemDevice::GetCrosvmArgs() const {
  std::string first = "--pmem";
  std::string read_only_arg = writable ? ",ro=false" : ",ro=true";
  std::string vma_size_arg;
  if (vma_size) {
    vma_size_arg = ",vma-size=" + std::to_string(vma_size.value());
  }
  std::string swap_interval_arg;
  if (swap_interval_ms) {
    swap_interval_arg =
        ",swap-interval-ms=" + std::to_string(swap_interval_ms.value());
  }
  std::string second =
      base::StrCat({path, read_only_arg, vma_size_arg, swap_interval_arg});
  base::StringPairs result = {{std::move(first), std::move(second)}};
  return result;
}

}  // namespace vm_tools::concierge
