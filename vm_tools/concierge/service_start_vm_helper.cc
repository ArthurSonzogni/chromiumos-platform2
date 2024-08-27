// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "vm_tools/concierge/service_start_vm_helper.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/version.h>

#include "base/files/scoped_file.h"
#include "vm_tools/concierge/dlc_helper.h"
#include "vm_tools/concierge/thread_utils.h"

namespace vm_tools {
namespace concierge {

std::optional<base::FilePath> Service::GetVmImagePath(
    const std::string& dlc_id, std::string& failure_reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<std::string> dlc_root = PostTaskAndWaitForResult(
      bus_->GetDBusTaskRunner(),
      base::BindOnce(
          [](DlcHelper* dlc_helper, const std::string& dlc_id,
             std::string& out_failure_reason) {
            return dlc_helper->GetRootPath(dlc_id, &out_failure_reason);
          },
          dlcservice_client_.get(), dlc_id, std::ref(failure_reason)));
  if (!dlc_root.has_value()) {
    // On an error, failure_reason will be set by GetRootPath().
    return {};
  }
  return base::FilePath(dlc_root.value());
}
namespace internal {

// TODO(b/213090722): Determining a VM's type based on its properties like
// this is undesirable. Instead we should provide the type in the request, and
// determine its properties from that.
apps::VmType ClassifyVm(const StartVmRequest& request) {
  // Identify Baguette VM by vm_type only
  if (request.vm_type() == VmInfo::BAGUETTE) {
    return apps::VmType::BAGUETTE;
  }
  if (request.vm_type() == VmInfo::BOREALIS ||
      request.vm().dlc_id() == kBorealisBiosDlcId) {
    return apps::VmType::BOREALIS;
  }
  if (request.vm_type() == VmInfo::TERMINA || request.start_termina()) {
    return apps::VmType::TERMINA;
  }
  // Bruschetta VMs are distinguished by having a separate bios as a dlc.
  if (request.vm_type() == VmInfo::BRUSCHETTA ||
      request.vm().dlc_id() == kBruschettaBiosDlcId ||
      request.name() == "bru") {
    return apps::VmType::BRUSCHETTA;
  }
  return apps::VmType::UNKNOWN;
}

// Get capacity & cluster & affinity information for cpu0~cpu${cpus}
VmBuilder::VmCpuArgs GetVmCpuArgs(int32_t cpus,
                                  const base::FilePath& cpu_info_path) {
  VmBuilder::VmCpuArgs result;
  // Group the CPUs by their physical package ID to determine CPU cluster
  // layout.
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;
  std::vector<std::string> cpu_capacity;
  for (int32_t cpu = 0; cpu < cpus; cpu++) {
    std::optional<int32_t> physical_package_id =
        GetCpuPackageId(cpu, cpu_info_path);
    if (physical_package_id) {
      CHECK_GE(*physical_package_id, 0);
      if (*physical_package_id + 1 > cpu_clusters.size()) {
        cpu_clusters.resize(*physical_package_id + 1);
      }
      cpu_clusters[*physical_package_id].push_back(std::to_string(cpu));
    }

    std::optional<int32_t> capacity = GetCpuCapacity(cpu, cpu_info_path);
    if (capacity) {
      CHECK_GE(*capacity, 0);
      cpu_capacity.push_back(base::StringPrintf("%d=%d", cpu, *capacity));
      auto group = cpu_capacity_groups.find(*capacity);
      if (group != cpu_capacity_groups.end()) {
        group->second.push_back(std::to_string(cpu));
      } else {
        std::initializer_list<std::string> g = {std::to_string(cpu)};
        cpu_capacity_groups.insert({*capacity, g});
      }
    }
  }

  std::optional<std::string> cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  if (cpu_affinity) {
    result.cpu_affinity = *cpu_affinity;
  }
  result.cpu_capacity = std::move(cpu_capacity);
  result.cpu_clusters = std::move(cpu_clusters);
  return result;
}

VMImageSpec GetImageSpec(const std::optional<base::ScopedFD>& kernel_fd,
                         const std::optional<base::ScopedFD>& rootfs_fd,
                         const std::optional<base::ScopedFD>& initrd_fd,
                         const std::optional<base::ScopedFD>& bios_fd,
                         const std::optional<base::ScopedFD>& pflash_fd,
                         const std::optional<base::FilePath>& biosDlcPath,
                         const std::optional<base::FilePath>& vmDlcPath,
                         const std::optional<base::FilePath>& toolsDlcPath,
                         std::string& failure_reason) {
  DCHECK(failure_reason.empty());
  DCHECK_CALLED_ON_VALID_SEQUENCE(base::SequenceChecker);

  base::FilePath kernel, rootfs, initrd, bios, pflash;
  if (kernel_fd.has_value()) {
    const base::ScopedFD& fd = kernel_fd.value();
    failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      return {};
    }
    kernel = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(fd.get()));
  } else if (vmDlcPath.has_value()) {
    kernel = vmDlcPath.value().Append(kVmKernelName);
  }

  if (rootfs_fd.has_value()) {
    const base::ScopedFD& fd = rootfs_fd.value();
    failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      return {};
    }
    rootfs = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(fd.get()));
  } else if (vmDlcPath.has_value()) {
    rootfs = vmDlcPath.value().Append(kVmRootfsName);
  }

  if (initrd_fd.has_value()) {
    const base::ScopedFD& fd = initrd_fd.value();
    failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      return {};
    }
    initrd = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(fd.get()));
  }

  if (bios_fd.has_value()) {
    const base::ScopedFD& fd = bios_fd.value();
    failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      return {};
    }
    bios = base::FilePath(kProcFileDescriptorsPath)
               .Append(base::NumberToString(fd.get()));
  } else if (biosDlcPath.has_value() && !biosDlcPath->empty()) {
    bios = biosDlcPath.value();
    bios = bios.Append(kBruschettaBiosDlcPath);
  }

  if (pflash_fd.has_value()) {
    const base::ScopedFD& fd = pflash_fd.value();
    failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      return {};
    }
    pflash = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(fd.get()));
  }

  base::FilePath tools_disk;
  if (toolsDlcPath.has_value()) {
    tools_disk = toolsDlcPath.value().Append(kVmToolsDiskName);
  } else if (vmDlcPath.has_value()) {
    tools_disk = vmDlcPath.value().Append(kVmToolsDiskName);
  }

  return VMImageSpec{
      .kernel = std::move(kernel),
      .initrd = std::move(initrd),
      .rootfs = std::move(rootfs),
      .bios = std::move(bios),
      .pflash = std::move(pflash),
      .tools_disk = std::move(tools_disk),
  };
}

std::string RemoveCloseOnExec(const base::ScopedFD& fd) {
  if (!fd.is_valid()) {
    return "Invalid fd";
  }
  int raw_fd = fd.get();

  int flags = fcntl(raw_fd, F_GETFD);
  if (flags == -1) {
    return "Failed to get flags for passed fd";
  }
  flags &= ~FD_CLOEXEC;
  if (fcntl(raw_fd, F_SETFD, flags) == -1) {
    return "Failed to clear close-on-exec flag for fd";
  }
  return "";
}

std::optional<VhostUserSocketPair> SetupVhostUserSocketPair() {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == -1) {
    PLOG(ERROR) << "Failed to create socket pair for test";
    return std::nullopt;
  }

  return VhostUserSocketPair{
      .front_end_fd = base::ScopedFD{fds[0]},
      .back_end_fd = base::ScopedFD(fds[1]),
  };
}

std::vector<base::ScopedFD> ScopedFDToVector(base::ScopedFD fd) {
  std::vector<base::ScopedFD> fds;
  fds.emplace_back(std::move(fd));
  return fds;
}

}  // namespace internal
}  // namespace concierge
}  // namespace vm_tools
