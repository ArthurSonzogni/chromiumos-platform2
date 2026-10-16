// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "vm_tools/concierge/service_start_vm_helper.h"

#include <fcntl.h>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <base/version.h>

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

namespace {

// Opens a VM image component from a DLC directory using OpenPathSafely.
// Returns an invalid ScopedFD if |dlc_dir| is not provided.
// Returns an error string on failure.
base::expected<base::ScopedFD, std::string> ResolveDlcImage(
    brillo::SafeFD& root_fd,
    const std::optional<base::FilePath>& dlc_dir,
    const char* image_name,
    int flags) {
  if (!dlc_dir || dlc_dir->empty()) {
    return base::ScopedFD();
  }

  base::FilePath image_path = dlc_dir->Append(image_name);
  base::expected<base::ScopedFD, std::string> fd =
      OpenPathSafely(root_fd, image_path, flags);
  if (!fd.has_value()) {
    return base::unexpected(base::StringPrintf(
        "Could not open %s from %s", image_name, dlc_dir->value().c_str()));
  }
  return std::move(fd.value());
}

}  // namespace

base::expected<VMImageSpec, std::string> AugmentImageSpec(
    brillo::SafeFD& root_fd,
    bool is_rootfs_writable,
    const std::optional<base::FilePath>& bios_dlc_path,
    const std::optional<base::FilePath>& vm_dlc_path,
    const std::optional<base::FilePath>& tools_dlc_path,
    VMImageSpec spec) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(base::SequenceChecker);

  if (!spec.bios.is_valid()) {
    base::expected<base::ScopedFD, std::string> bios = ResolveDlcImage(
        root_fd, bios_dlc_path, kBruschettaBiosDlcPath, O_RDONLY);
    if (!bios.has_value()) {
      return base::unexpected(bios.error());
    }
    spec.bios = std::move(bios.value());
  }

  if (!spec.kernel.is_valid()) {
    std::optional<base::FilePath> kernel_dlc = vm_dlc_path;
    if (!kernel_dlc && !spec.bios.is_valid()) {
      kernel_dlc = tools_dlc_path;
    }
    base::expected<base::ScopedFD, std::string> kernel =
        ResolveDlcImage(root_fd, kernel_dlc, kVmKernelName, O_RDONLY);
    if (!kernel.has_value()) {
      return base::unexpected(kernel.error());
    }
    spec.kernel = std::move(kernel.value());
  }

  if (!spec.kernel.is_valid() && !spec.bios.is_valid()) {
    return base::unexpected("neither a kernel nor a BIOS were provided");
  }

  if (!spec.rootfs.is_valid()) {
    int rootfs_flags = is_rootfs_writable ? O_RDWR : O_RDONLY;
    base::expected<base::ScopedFD, std::string> rootfs =
        ResolveDlcImage(root_fd, vm_dlc_path, kVmRootfsName, rootfs_flags);
    if (!rootfs.has_value()) {
      return base::unexpected(rootfs.error());
    }
    spec.rootfs = std::move(rootfs.value());
  }

  if (!spec.tools_disk.is_valid()) {
    std::optional<base::FilePath> tools_dlc =
        tools_dlc_path ? tools_dlc_path : vm_dlc_path;
    base::expected<base::ScopedFD, std::string> tools =
        ResolveDlcImage(root_fd, tools_dlc, kVmToolsDiskName, O_RDONLY);
    // tools_disk is optional, so ignore errors if opening fails.
    if (tools.has_value()) {
      spec.tools_disk = std::move(tools.value());
    }
  }

  return spec;
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

}  // namespace internal
}  // namespace concierge
}  // namespace vm_tools
