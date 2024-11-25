// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_BUILDER_H_
#define VM_TOOLS_CONCIERGE_VM_BUILDER_H_

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_split.h>
#include <dbus/object_proxy.h>

#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

class VmBuilder {
 public:
  // Describe the values for --async-executor options passed to crosvm
  enum class AsyncExecutor {
    kUring,
    kEpoll,
  };

  // VM block storage device.
  struct Disk {
    // Gets the command line argument that needs to be passed to crosvm
    // corresponding to this disk.
    base::StringPairs GetCrosvmArgs() const;

    // Path to the disk image on the host.
    base::FilePath path;

    // Whether the disk should be writable by the VM.
    bool writable;

    // Whether the disk should allow sparse file operations (discard) by the VM.
    std::optional<bool> sparse;

    // Whether the disk access should be done with O_DIRECT by the VM.
    std::optional<bool> o_direct;

    // Whether to enable multiple workers
    std::optional<bool> multiple_workers;

    // Async executor crosvm should use to run the disk devices.
    std::optional<AsyncExecutor> async_executor;

    // Block size.
    std::optional<size_t> block_size;

    // Block ID (max 20 chars).
    std::optional<std::string> block_id;
  };

  // Contains the rootfs device and path.
  struct Rootfs {
    std::string device;
    base::FilePath path;
    bool writable;
  };

  // Args related to CPU configuration for a VM.
  struct VmCpuArgs {
    std::string cpu_affinity;
    std::vector<std::string> cpu_capacity;
    std::vector<std::vector<std::string>> cpu_clusters;
  };

  struct PmemDevice {
    // Gets the command line argument that needs to be passed to crosvm
    // corresponding to this pmem device.
    base::StringPairs GetCrosvmArgs() const;

    // Path to the disk image on the host, or an anonymous virtual memory area
    // name when the vma_size is set.
    std::string path;

    // Whether the disk should be writable by the VM.
    bool writable;

    // Size in bytes for an anonymous memory area to be created to back this
    // pmem device.
    std::optional<uint64_t> vma_size;

    // The interval in ms between swapping out the memory mapped by this pmem
    // device.
    std::optional<uint64_t> swap_interval_ms;
  };

  VmBuilder();
  VmBuilder(VmBuilder&&);
  VmBuilder& operator=(VmBuilder&& other);
  VmBuilder(const VmBuilder&) = delete;
  VmBuilder& operator=(const VmBuilder&) = delete;
  ~VmBuilder();

  VmBuilder& SetKernel(base::FilePath kernel);
  VmBuilder& SetInitrd(base::FilePath initrd);
  VmBuilder& SetBios(base::FilePath bios);
  VmBuilder& SetPflash(base::FilePath pflash);
  VmBuilder& SetRootfs(const struct Rootfs& rootfs);
  VmBuilder& SetCpus(int32_t cpus);
  VmBuilder& SetVmCpuArgs(const struct VmCpuArgs& vm_cpu_args);
  VmBuilder& SetVsockCid(uint32_t vsock_cid);
  VmBuilder& AppendDisk(Disk disk);
  VmBuilder& SetMemory(std::string_view memory_in_mb);
  VmBuilder& SetBalloonBias(std::string_view balloon_bias_mib);
  VmBuilder& EnableWorkingSetReporting(bool enable);

  VmBuilder& SetSyslogTag(std::string_view syslog_tag);
  VmBuilder& SetSocketPath(std::string_view socket_path);
  VmBuilder& AppendTapFd(base::ScopedFD tap_fd);
  VmBuilder& AppendKernelParam(std::string_view param);
  VmBuilder& AppendOemString(std::string_view string);
  VmBuilder& AppendAudioDevice(std::string_view params);
  VmBuilder& AppendSerialDevice(std::string_view device);
  VmBuilder& AppendPmemDevice(PmemDevice device);
  VmBuilder& AppendSharedDir(SharedDataParam shared_data_param);
  VmBuilder& AppendCustomParam(std::string_view key, std::string_view value);
  VmBuilder& AppendVhostUserFsFrontend(VhostUserFsFrontParam param);

  // Instructs this VM to use a wayland socket, if the empty string is provided
  // the default path to the socket will be used, otherwise |socket| will be the
  // path.
  VmBuilder& SetWaylandSocket(std::string_view socket = "");
  VmBuilder& AddExtraWaylandSocket(std::string_view socket);

  VmBuilder& EnableGpu(bool enable);
  VmBuilder& EnableDGpuPassthrough(bool enable);
  VmBuilder& EnableVulkan(bool enable);
  // Make virglrenderer use Big GL instead of the default GLES.
  VmBuilder& EnableBigGl(bool enable);
  // Offload Vulkan use to isolated virglrenderer render server
  VmBuilder& EnableRenderServer(bool enable);
  VmBuilder& SetGpuCachePath(base::FilePath gpu_cache_path);
  VmBuilder& SetGpuCacheSize(std::string_view gpu_cache_size_str);
  VmBuilder& SetRenderServerCachePath(base::FilePath render_server_cache_path);
  VmBuilder& SetPrecompiledCachePath(base::FilePath precompiled_cache_path);
  VmBuilder& SetFozDbListPath(base::FilePath foz_db_list_path);
  VmBuilder& SetRenderServerCacheSize(
      std::string_view render_server_cache_size_str);
  // By default, VMM infers which context types should be advertised.
  // Enabling any specific context type via these API functions instructs VMM to
  // skip its inference and only advertise those enabled explicitly. Calling
  // EnableGpuContextTypeDefaults() reverts all previous settings back to the
  // VMM-inferred context types.
  VmBuilder& EnableGpuContextTypeDefaults();
  VmBuilder& EnableGpuContextTypeVirgl(bool enable);
  VmBuilder& EnableGpuContextTypeVenus(bool enable);
  VmBuilder& EnableGpuContextTypeCrossDomain(bool enable);
  VmBuilder& EnableGpuContextTypeDrm(bool enable);

  VmBuilder& EnableVtpmProxy(bool enable);
  VmBuilder& EnableVideoDecoder(bool enable);
  // Sets the video decoder to use. This is only used if
  // EnableVideoDecoder(true) is called.
  VmBuilder& SetVideoDecoder(std::string_view video_decoder);
  VmBuilder& EnableVideoEncoder(bool enable);
  VmBuilder& EnableBattery(bool enable);
  VmBuilder& EnableSmt(bool enable);
  VmBuilder& EnableDelayRt(bool enable);
  VmBuilder& EnablePerVmCoreScheduling(bool enable);
  VmBuilder& EnablePvClock(bool enable);

  // Override flags for O_DIRECT for already appended Nth disk.
  VmBuilder& EnableODirectN(int n, bool enable);
  // Override flags for multiple_workers for already appended disks.
  VmBuilder& EnableMultipleWorkers(bool enable);
  // Override options for the async runtime for already appended disks.
  VmBuilder& SetBlockAsyncExecutor(AsyncExecutor executor);
  // Override block size for already appended disks.
  VmBuilder& SetBlockSize(size_t block_size);
  VmBuilder& SetBlockSizeN(size_t n, size_t block_size);

  VmBuilder& SetVmmSwapDir(base::FilePath vmm_swap_dir);

  // Builds the command line required to start a VM. Optionally Applies
  // dev_params to modify the configuration. Consumes this (the builder).
  // Returns std::nullopt on failure.
  std::optional<base::StringPairs> BuildVmArgs(
      CustomParametersForDev* dev_params) &&;

  static void SetValidWaylandRegexForTesting(char* regex);

 private:
  // note: used as unsigned "position" index in std::bitset
  enum GpuContextType : unsigned int {
    GPU_CONTEXT_TYPE_VIRGL,
    GPU_CONTEXT_TYPE_VENUS,
    GPU_CONTEXT_TYPE_CROSS_DOMAIN,
    GPU_CONTEXT_TYPE_DRM,

    // note: must remain last
    GPU_CONTEXT_TYPE_COUNT,
  };

  bool HasValidWaylandSockets() const;

  // Builds the parameters for `crosvm run` to start a VM based on this
  // VmBuilder's settings.
  base::StringPairs BuildRunParams() const;
  // Builds the parameters between `crosvm` and `run`.
  base::StringPairs BuildPreRunParams() const;
  bool ProcessCustomParameters(const CustomParametersForDev& devparams);

  base::FilePath kernel_;
  base::FilePath initrd_;
  base::FilePath bios_;
  base::FilePath pflash_;
  std::optional<Rootfs> rootfs_;
  int32_t cpus_ = 0;
  std::optional<VmCpuArgs> vm_cpu_args_;
  std::optional<uint32_t> vsock_cid_;
  std::string memory_in_mib_;
  std::string balloon_bias_mib_;

  std::string syslog_tag_;
  std::string vm_socket_path_;

  bool enable_gpu_ = false;
  bool enable_dgpu_passthrough_ = false;
  bool enable_vulkan_ = false;

  bool enable_big_gl_ = false;
  bool enable_render_server_ = false;
  base::FilePath gpu_cache_path_;
  std::string gpu_cache_size_str_;
  base::FilePath render_server_cache_path_;
  base::FilePath foz_db_list_path_;
  base::FilePath precompiled_cache_path_;
  std::string render_server_cache_size_str_;
  std::bitset<GPU_CONTEXT_TYPE_COUNT> enabled_gpu_context_types_;

  bool enable_vtpm_proxy_ = false;
  bool enable_video_decoder_ = false;
  std::string video_decoder_ = "libvda";
  bool enable_video_encoder_ = false;
  bool enable_battery_ = false;
  bool enable_pvclock_ = false;
  std::optional<bool> enable_smt_ = false;
  bool enable_delay_rt_ = false;
  bool enable_per_vm_core_scheduling_ = false;
  bool enable_working_set_reporting_ = false;

  std::vector<Disk> disks_;
  std::vector<std::string> kernel_params_;
  std::vector<std::string> oem_strings_;
  std::vector<base::ScopedFD> tap_fds_;

  struct AudioDevice {
    std::string params;
  };
  std::vector<AudioDevice> audio_devices_;
  std::vector<std::string> serial_devices_;
  std::vector<std::string> wayland_sockets_;
  std::vector<SharedDataParam> shared_dirs_;
  std::vector<std::vector<int32_t>> cpu_clusters_;
  std::vector<PmemDevice> pmem_devices_;
  std::vector<VhostUserFsFrontParam> vhost_user_fs_fronts_;

  base::FilePath vmm_swap_dir_;

  base::StringPairs custom_params_;
};

// Convert a string to the corresponding AsyncExecutor. This returns nullopt if
// the given string is unknown.
std::optional<VmBuilder::AsyncExecutor> StringToAsyncExecutor(
    std::string_view async_executor);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VM_BUILDER_H_
