// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <optional>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/strings/string_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <re2/re2.h>

#include "base/files/file_path.h"
#include "vm_tools/concierge/pci_utils.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {
namespace {
// Path to the default wayland socket.
constexpr char kWaylandSocket[] = "/run/chrome/wayland-0";

constexpr char kVirglRenderServerPath[] = "/usr/libexec/virgl_render_server";
constexpr char kVvuProxySocketPathFormat[] = "/run/crosvm-vvu%02d.sock";

constexpr char kResourceBridgeNameFormat[] = "resource_bridge%02zu.sock";

// Returns the common part of the command line for invoking different type of
// VVU devices. This will be like "crosvm device --vfio <pci address>".
base::StringPairs BuildVvuBaseCmd(std::string device,
                                  const base::FilePath& vvu_proxy_device_path) {
  base::StringPairs cmd = {{kCrosvmBin, "device"}};
  cmd.emplace_back(device, "");
  std::string vvu_pci_addr = vvu_proxy_device_path.BaseName().MaybeAsASCII();
  cmd.emplace_back("--vfio", std::move(vvu_pci_addr));
  return cmd;
}

// Returns the command line for spawning the net VVU device backend.
base::StringPairs BuildVvuNetCmd(int32_t tap_fd,
                                 const base::FilePath& vvu_proxy_device_path) {
  base::StringPairs cmd = {{kCrosvmBin, "device"}};
  cmd.emplace_back("net", "");
  std::string arg = vvu_proxy_device_path.BaseName().MaybeAsASCII();
  arg += "," + std::to_string(tap_fd);
  cmd.emplace_back("--vvu-tap-fd", std::move(arg));
  return cmd;
}

// Returns the command line for spawning a sibling VM.
std::vector<std::string> BuildBaseSiblingArgs() {
  std::vector<std::string> args = {
      "run",
      // TODO(b/196186396): Eventually enable sandbox for sibling VMs.
      "--disable-sandbox",
      "-p",
      "console=hvc0",
      "-p",
      "root=/dev/vda",
  };
  return args;
}

// Returns the socket path associated with the proxy of a VVU device.
inline std::string BuildVvuSocketPath(int32_t index) {
  return base::StringPrintf(kVvuProxySocketPathFormat, index);
}

// Takes a VVU device from the vector of un-allocated devices.
std::optional<VvuDeviceInfo> TakeNextVvuDevice(
    std::vector<VvuDeviceInfo>* info) {
  if (info->empty()) {
    return std::nullopt;
  }
  auto ret = std::make_optional(info->back());
  info->pop_back();
  return ret;
}

// Allocates the next resource bridge path, returning the path string.
std::string AllocateResourceBridgePath(
    const base::FilePath& runtime_dir,
    std::vector<std::string>* resource_bridges) {
  auto bridge_name =
      base::StringPrintf(kResourceBridgeNameFormat, resource_bridges->size());
  auto bridge_path = runtime_dir.Append(bridge_name).value();
  resource_bridges->push_back(bridge_path);
  return bridge_path;
}

}  // namespace

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

VmBuilder& VmBuilder::SetVsockCid(uint32_t vsock_cid) {
  vsock_cid_ = vsock_cid;
  return *this;
}

VmBuilder& VmBuilder::AppendDisks(std::vector<Disk> disks) {
  disks_ = std::move(disks);
  return *this;
}

VmBuilder& VmBuilder::SetMemory(const std::string& memory_in_mb) {
  memory_in_mib_ = memory_in_mb;
  return *this;
}

VmBuilder& VmBuilder::SetBalloonBias(const std::string& balloon_bias_mib) {
  balloon_bias_mib_ = balloon_bias_mib;
  return *this;
}

VmBuilder& VmBuilder::SetSyslogTag(const std::string& syslog_tag) {
  syslog_tag_ = syslog_tag;
  return *this;
}

VmBuilder& VmBuilder::SetSocketPath(const std::string& socket_path) {
  vm_socket_path_ = socket_path;
  return *this;
}

VmBuilder& VmBuilder::AppendTapFd(base::ScopedFD fd) {
  tap_fds_.push_back(std::move(fd));
  return *this;
}

VmBuilder& VmBuilder::AppendKernelParam(const std::string& param) {
  kernel_params_.push_back(param);
  return *this;
}

VmBuilder& VmBuilder::AppendOemString(const std::string& string) {
  oem_strings_.push_back(string);
  return *this;
}

VmBuilder& VmBuilder::AppendAudioDevice(const AudioDeviceType type,
                                        const std::string& params) {
  audio_devices_.push_back(AudioDevice{.type = type, .params = params});
  return *this;
}

VmBuilder& VmBuilder::AppendSerialDevice(const std::string& device) {
  serial_devices_.push_back(device);
  return *this;
}

VmBuilder& VmBuilder::SetWaylandSocket(const std::string& socket) {
  // The "true" socket, which is the visual one, must be set first.
  DCHECK(wayland_sockets_.empty());
  if (socket.empty()) {
    // We want the empty string to mean "use the default socket", since that is
    // the behaviour we want if the user does not set the wayland socket in the
    // VirtualMachineSpec proto.
    wayland_sockets_.push_back(kWaylandSocket);
  } else {
    wayland_sockets_.push_back(socket);
  }
  return *this;
}

VmBuilder& VmBuilder::AddExtraWaylandSocket(const std::string& socket) {
  // Additional sockets must only be added after the "true" socket, since the
  // first socket provided to the VM will always be interpreted as the visual
  // one.
  DCHECK(!wayland_sockets_.empty());
  wayland_sockets_.push_back(socket);
  return *this;
}

VmBuilder& VmBuilder::AppendSharedDir(const std::string& shared_dir) {
  shared_dirs_.push_back(shared_dir);
  return *this;
}

VmBuilder& VmBuilder::AppendCustomParam(const std::string& key,
                                        const std::string& value) {
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

VmBuilder& VmBuilder::EnableVirtgpuNativeContext(bool enable) {
  enable_virtgpu_native_context_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableCrossDomainContext(bool enable) {
  enable_cross_domain_context_ = enable;
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

VmBuilder& VmBuilder::SetGpuCacheSize(std::string gpu_cache_size_str) {
  gpu_cache_size_str_ = std::move(gpu_cache_size_str);
  return *this;
}

VmBuilder& VmBuilder::SetRenderServerCachePath(
    base::FilePath render_server_cache_path) {
  render_server_cache_path_ = std::move(render_server_cache_path);
  return *this;
}

VmBuilder& VmBuilder::SetFozDbListPath(base::FilePath foz_db_list_path) {
  foz_db_list_path_ = std::move(foz_db_list_path);
  return *this;
}

VmBuilder& VmBuilder::SetRenderServerCacheSize(
    std::string render_server_cache_size_str) {
  render_server_cache_size_str_ = std::move(render_server_cache_size_str);
  return *this;
}

VmBuilder& VmBuilder::EnableSoftwareTpm(bool enable) {
  enable_software_tpm_ = enable;
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

VmBuilder& VmBuilder::EnableODirect(bool enable) {
  for (auto& d : disks_) {
    d.o_direct = enable;
  }
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

VmBuilder& VmBuilder::SetVmMemoryId(VmMemoryId vm_memory_id) {
  vm_memory_id_ = std::make_optional(vm_memory_id);
  return *this;
}

VmBuilder& VmBuilder::SetVmmSwapDir(base::FilePath vmm_swap_dir) {
  vmm_swap_dir_ = std::move(vmm_swap_dir);
  return *this;
}

base::StringPairs VmBuilder::BuildVmArgs(
    CustomParametersForDev* devparams) const {
  base::StringPairs args = BuildRunParams();

  // Early-return when BuildRunParams() failed.
  if (args.empty())
    return args;

  if (devparams)
    devparams->Apply(&args);

  args.emplace(args.begin(), kCrosvmBin, "run");

  // Kernel should be at the end.
  if (!kernel_.empty())
    args.emplace_back(kernel_.value(), "");

  return args;
}

base::StringPairs VmBuilder::BuildRunParams() const {
  base::StringPairs args = {{"--cpus", std::to_string(cpus_)}};

  if (!memory_in_mib_.empty())
    args.emplace_back("--mem", memory_in_mib_);

  if (!balloon_bias_mib_.empty())
    args.emplace_back("--balloon-bias-mib", balloon_bias_mib_);

  for (const auto& tap_fd : tap_fds_)
    args.emplace_back("--net", "tap-fd=" + std::to_string(tap_fd.get()));

  if (vsock_cid_.has_value())
    args.emplace_back("--cid", std::to_string(vsock_cid_.value()));

  if (!vm_socket_path_.empty())
    args.emplace_back("--socket", vm_socket_path_);

  for (const auto& w : wayland_sockets_)
    args.emplace_back("--wayland-sock", w);

  for (const auto& s : serial_devices_)
    args.emplace_back("--serial", s);

  if (!syslog_tag_.empty())
    args.emplace_back("--syslog-tag", syslog_tag_);

  if (enable_smt_.has_value()) {
    if (!enable_smt_.value())
      args.emplace_back("--no-smt", "");
  }

  if (enable_delay_rt_)
    args.emplace_back("--delay-rt", "");

  if (enable_per_vm_core_scheduling_)
    args.emplace_back("--per-vm-core-scheduling", "");

  if (kernel_params_.size() > 0)
    args.emplace_back("--params", base::JoinString(kernel_params_, " "));

  for (const std::string& s : oem_strings_)
    args.emplace_back("--oem-strings", s);

  if (rootfs_.has_value()) {
    const auto& rootfs = rootfs_.value();
    if (rootfs.device.find("pmem") != std::string::npos) {
      if (rootfs.writable) {
        args.emplace_back("--rw-pmem-device", rootfs.path.value());
      } else {
        args.emplace_back("--pmem-device", rootfs.path.value());
      }
      // TODO(davidriley): Re-add rootflags=dax once guest kernel has fix for
      // b/169339326.
      args.emplace_back("--params", "root=/dev/pmem0 ro");
    } else {
      if (rootfs.writable) {
        args.emplace_back("--rwroot", rootfs.path.value());
      } else {
        args.emplace_back("--root", rootfs.path.value());
      }
    }
  }

  for (const auto& dev : audio_devices_) {
    switch (dev.type) {
      case AudioDeviceType::kAC97:
        args.emplace_back("--ac97", dev.params);
        break;
      case AudioDeviceType::kVirtio:
        args.emplace_back("--virtio-snd", dev.params);
        break;
    }
  }

  for (const auto& d : disks_) {
    auto disk_args = d.GetCrosvmArgs();
    args.insert(std::end(args), std::begin(disk_args), std::end(disk_args));
  }

  if (enable_gpu_) {
    std::string gpu_arg = "vulkan=";
    gpu_arg += enable_vulkan_ ? "true" : "false";
    if (enable_cross_domain_context_) {
      gpu_arg += ",context-types=cross-domain";
      if (enable_vulkan_) {
        gpu_arg += ":venus";
      }
      if (enable_virtgpu_native_context_) {
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
  }

  if (enable_software_tpm_)
    args.emplace_back("--software-tpm", "");

  if (enable_vtpm_proxy_)
    args.emplace_back("--vtpm-proxy", "");

  if (enable_video_decoder_)
    args.emplace_back("--video-decoder", "libvda");

  if (enable_video_encoder_)
    args.emplace_back("--video-encoder", "libvda");

  if (enable_battery_)
    args.emplace_back("--battery", "type=goldfish");

  for (const auto& shared_dir : shared_dirs_)
    args.emplace_back("--shared-dir", shared_dir);

  for (const auto& custom_param : custom_params_)
    args.emplace_back(custom_param.first, custom_param.second);

  if (!initrd_.empty())
    args.emplace_back("-i", initrd_.value());

  if (!bios_.empty())
    args.emplace_back("--bios", bios_.value());

  if (!pflash_.empty())
    args.emplace_back("--pflash", "path=" + pflash_.value());

  if (vm_memory_id_) {
    std::string mms_control_socket = "/run/mms_control_";
    mms_control_socket += std::to_string(*vm_memory_id_);
    mms_control_socket += ".sock";
    args.emplace_back("--balloon-control", mms_control_socket);
  }

  if (!vmm_swap_dir_.empty()) {
    args.emplace_back("--swap", vmm_swap_dir_.value());
  }

  return args;
}

std::optional<VmBuilder::SiblingStartCommands> VmBuilder::BuildSiblingCmds(
    std::vector<VvuDeviceInfo> vvu_devices_info,
    const base::ScopedTempDir& runtime_dir) const {
  size_t num_vvu_devices = vvu_devices_info.size();
  VmBuilder::SiblingStartCommands cmds;
  cmds.sibling_cmd_args = BuildBaseSiblingArgs();
  std::vector<std::string> resource_bridges;

  // Console VVU device.
  for (size_t i = 0; i < serial_devices_.size(); ++i) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuBaseCmd("console", vvu_device_info->proxy_device);
    // TODO(b/215472603): Specify input-file and output-file.
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-console",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Rootfs Block VVU device.
  if (rootfs_.has_value()) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuBaseCmd("block", vvu_device_info->proxy_device);
    const auto& rootfs = rootfs_.value();
    if (rootfs.writable) {
      cmd.emplace_back("--file", rootfs.path.value());
    } else {
      cmd.emplace_back("--file", rootfs.path.value() + ":read-only");
    }
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-blk",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Tools and Stateful Block VVU device.
  for (const auto& d : disks_) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuBaseCmd("block", vvu_device_info->proxy_device);
    auto disk_args = d.GetVvuArgs();
    cmd.insert(std::end(cmd), std::begin(disk_args), std::end(disk_args));
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-blk",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Vsock VVU device.
  if (vsock_cid_.has_value()) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuBaseCmd("vsock", vvu_device_info->proxy_device);
    cmd.emplace_back("--cid", std::to_string(vsock_cid_.value()));
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-vsock",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Net VVU device.
  for (const auto& tap_fd : tap_fds_) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuNetCmd(tap_fd.get(), vvu_device_info->proxy_device);
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-net",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Sound VVU device.
  for (const auto& audio_device : audio_devices_) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }
    base::StringPairs cmd =
        BuildVvuBaseCmd("snd", vvu_device_info->proxy_device);
    cmd.emplace_back("--config", audio_device.params);
    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-snd",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // Wayland device.
  if (!wayland_sockets_.empty()) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    base::StringPairs cmd =
        BuildVvuBaseCmd("wl", vvu_device_info->proxy_device);
    for (const auto& w : wayland_sockets_) {
      cmd.emplace_back("--wayland-sock", w);
    }

    if (enable_gpu_) {
      cmd.emplace_back(
          "--resource-bridge",
          AllocateResourceBridgePath(runtime_dir.GetPath(), &resource_bridges));
    }

    cmds.vvu_cmds.emplace_back(cmd);
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--vhost-user-wl",
         BuildVvuSocketPath(vvu_device_info->proxy_socket_index)});
  }

  // GPU device
  if (enable_gpu_) {
    auto vvu_device_info = TakeNextVvuDevice(&vvu_devices_info);
    if (!vvu_device_info) {
      LOG(ERROR) << "Not enough socket indices: " << num_vvu_devices;
      return std::nullopt;
    }

    if (!AddGpuSiblingCmd(*vvu_device_info, resource_bridges, &cmds)) {
      return std::nullopt;
    }
  }

  // TODO(morg): Refactor shared parameter logic with BuildVmArgs
  cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(),
                               {"--cpus", std::to_string(cpus_)});

  if (vm_memory_id_) {
    std::string mms_control_socket =
        "/run/mms_control_" + std::to_string(*vm_memory_id_) + ".sock";
    cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(),
                                 {"--balloon-control", mms_control_socket});
  }

  if (!memory_in_mib_.empty()) {
    cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(),
                                 {"--mem", memory_in_mib_});
  }

  if (enable_smt_.has_value()) {
    if (!enable_smt_.value())
      cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(), "--no-smt");
  }

  if (enable_delay_rt_)
    cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(), "--delay-rt");

  if (enable_per_vm_core_scheduling_)
    cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(),
                                 "--per-vm-core-scheduling");

  if (kernel_params_.size() > 0)
    cmds.sibling_cmd_args.insert(
        cmds.sibling_cmd_args.end(),
        {"--params", base::JoinString(kernel_params_, " ")});

  // Enable strict ballooning, since manatee's strict memory management
  // is incompatible with deflate-on-oom.
  cmds.sibling_cmd_args.insert(cmds.sibling_cmd_args.end(), "--strict-balloon");

  return cmds;
}

bool VmBuilder::AddGpuSiblingCmd(
    const VvuDeviceInfo& vvu_device_info,
    const std::vector<std::string>& resource_bridges,
    SiblingStartCommands* cmds) const {
  base::StringPairs cmd = BuildVvuBaseCmd("gpu", vvu_device_info.proxy_device);

  base::Value params(base::Value::Type::DICT);
  params.SetKey("vulkan", base::Value(enable_vulkan_));
  params.SetKey("gles", base::Value(!enable_big_gl_));
  if (!gpu_cache_path_.empty()) {
    params.SetKey("cache-path", base::Value(gpu_cache_path_.value()));
  }
  if (!gpu_cache_size_str_.empty()) {
    params.SetKey("cache-size", base::Value(gpu_cache_size_str_));
  }

  std::string params_str;
  base::JSONWriter::Write(params, &params_str);
  cmd.emplace_back("--params", params_str);

  for (auto bridge : resource_bridges) {
    cmd.emplace_back("--resource-bridge", bridge);
  }

  cmds->vvu_cmds.emplace_back(cmd);
  cmds->sibling_cmd_args.insert(
      cmds->sibling_cmd_args.end(),
      {"--vhost-user-gpu",
       BuildVvuSocketPath(vvu_device_info.proxy_socket_index)});
  return true;
}

}  // namespace concierge
}  // namespace vm_tools
