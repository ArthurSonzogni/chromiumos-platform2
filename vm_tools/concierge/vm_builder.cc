// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <cassert>
#include <utility>

#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>

#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

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

VmBuilder& VmBuilder::AppendAudioDevice(const std::string& device) {
  audio_devices_.push_back(device);
  return *this;
}

VmBuilder& VmBuilder::AppendSerialDevice(const std::string& device) {
  serial_devices_.push_back(device);
  return *this;
}

VmBuilder& VmBuilder::AppendWaylandSocket(const std::string& socket) {
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
  EnableGpu(enable, "");
  return *this;
}

VmBuilder& VmBuilder::EnableGpu(bool enable, const std::string& gpu_arg) {
  enable_gpu_ = enable;
  gpu_arg_ = gpu_arg;
  return *this;
}

VmBuilder& VmBuilder::EnableWaylandDmaBuf(bool enable) {
  enable_wayland_dma_buf_ = enable;
  return *this;
}

VmBuilder& VmBuilder::EnableSoftwareTpm(bool enable) {
  enable_software_tpm_ = enable;
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

VmBuilder& VmBuilder::EnableRtVcpu(bool enable) {
  enable_rt_vcpu_ = enable;
  return *this;
}

base::StringPairs VmBuilder::BuildVmArgs() const {
  base::StringPairs args = {{kCrosvmBin, "run"}};

  args.emplace_back("--cpus", std::to_string(cpus_));

  // Add RT-vcpus following non RT-vcpus if enabled.
  // The number of non RT-vcpus is reduced by the number of RT-vcpus.
  if (enable_rt_vcpu_) {
    // Add one RT-vcpu.
    const int rt_vcpu_num = 1;
    // Ensure that non RT-vcpus exist.
    assert(cpus_ - rt_vcpu_num > 0);

    // Isolate non RT-vcpus and RT-vcpus.
    // Guarantee that any non RT-vcpus and any RT-vcpus are never assigned to
    // a same pCPU to avoid lock-holder preemption problem.
    const int pcpu_num_for_rt_vcpus = 1;
    std::vector<std::string> cpu_affinities;
    for (int i = 0; i < cpus_ - rt_vcpu_num; i++) {
      cpu_affinities.emplace_back(base::StringPrintf(
          "%d=%d-%d", i, 0, cpus_ - pcpu_num_for_rt_vcpus - 1));
    }
    for (int i = cpus_ - rt_vcpu_num; i < cpus_; i++) {
      cpu_affinities.emplace_back(base::StringPrintf(
          "%d=%d-%d", i, cpus_ - pcpu_num_for_rt_vcpus, cpus_ - 1));
    }
    args.emplace_back("--cpu-affinity", base::JoinString(cpu_affinities, ":"));

    std::vector<std::string> rt_vcpus;
    for (int i = cpus_ - rt_vcpu_num; i < cpus_; i++) {
      rt_vcpus.emplace_back(std::to_string(i));
    }
    const std::string s_rt_vcpus = base::JoinString(rt_vcpus, ",");
    args.emplace_back("--rt-cpus", s_rt_vcpus);
    args.emplace_back("--params", "isolcpus=" + s_rt_vcpus);
    args.emplace_back("--params", "androidboot.rtcpus=" + s_rt_vcpus);
  }

  if (!memory_in_mib_.empty())
    args.emplace_back("--mem", memory_in_mib_);

  if (!balloon_bias_mib_.empty())
    args.emplace_back("--balloon_bias_mib", balloon_bias_mib_);

  for (const auto& tap_fd : tap_fds_)
    args.emplace_back("--tap-fd", std::to_string(tap_fd.get()));

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

  if (kernel_params_.size() > 0)
    args.emplace_back("--params", base::JoinString(kernel_params_, " "));

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

  for (const auto& a : audio_devices_)
    args.emplace_back("--ac97", a);

  for (const auto& d : disks_) {
    auto disk_args = d.GetCrosvmArgs();
    args.insert(std::end(args), std::begin(disk_args), std::end(disk_args));
  }

  if (enable_wayland_dma_buf_)
    args.emplace_back("--wayland-dmabuf", "");

  if (enable_gpu_) {
    if (gpu_arg_.empty())
      args.emplace_back("--gpu", "");
    else
      args.emplace_back(gpu_arg_, "");
  }

  if (enable_software_tpm_)
    args.emplace_back("--software-tpm", "");

  if (enable_video_decoder_)
    args.emplace_back("--video-decoder", "");

  if (enable_video_encoder_)
    args.emplace_back("--video-encoder", "");

  if (enable_battery_)
    args.emplace_back("--battery", "");

  for (const auto& shared_dir : shared_dirs_)
    args.emplace_back("--shared-dir", shared_dir);

  for (const auto& custom_param : custom_params_)
    args.emplace_back(custom_param.first, custom_param.second);

  if (!initrd_.empty())
    args.emplace_back("-i", initrd_.value());

  // Kernel should be at the end.
  if (!kernel_.empty())
    args.emplace_back(kernel_.value(), "");

  return args;
}

}  // namespace concierge
}  // namespace vm_tools
