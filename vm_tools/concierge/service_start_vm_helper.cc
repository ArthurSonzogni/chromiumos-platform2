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

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/version.h"

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
  if (request.vm_type() == VmInfo::BOREALIS ||
      request.vm().dlc_id() == kBorealisBiosDlcId)
    return apps::VmType::BOREALIS;
  if (request.vm_type() == VmInfo::TERMINA || request.start_termina())
    return apps::VmType::TERMINA;
  // Bruschetta VMs are distinguished by having a separate bios, either as an FD
  // or a dlc.
  bool has_bios_fd =
      std::any_of(request.fds().begin(), request.fds().end(),
                  [](int type) { return type == StartVmRequest::BIOS; });
  if (request.vm_type() == VmInfo::BRUSCHETTA || has_bios_fd ||
      request.vm().dlc_id() == "edk2-ovmf-dlc" || request.name() == "bru")
    return apps::VmType::BRUSCHETTA;
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
      if (*physical_package_id + 1 > cpu_clusters.size())
        cpu_clusters.resize(*physical_package_id + 1);
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

VMImageSpec GetImageSpec(const vm_tools::concierge::VirtualMachineSpec& vm,
                         const std::optional<base::ScopedFD>& kernel_fd,
                         const std::optional<base::ScopedFD>& rootfs_fd,
                         const std::optional<base::ScopedFD>& initrd_fd,
                         const std::optional<base::ScopedFD>& bios_fd,
                         const std::optional<base::ScopedFD>& pflash_fd,
                         const std::optional<base::FilePath>& biosDlcPath,
                         const std::optional<base::FilePath>& vmDlcPath,
                         const std::optional<base::FilePath>& toolsDlcPath,
                         bool is_termina,
                         std::string& failure_reason) {
  DCHECK(failure_reason.empty());
  DCHECK_CALLED_ON_VALID_SEQUENCE(base::SequenceChecker);
  // A VM image is trusted when both:
  // 1) This daemon (or a trusted daemon) chooses the kernel and rootfs path.
  // 2) The chosen VM is a first-party VM.
  // In practical terms this is true iff we are booting termina without
  // specifying kernel and rootfs image.
  bool is_trusted_image = is_termina;

  base::FilePath kernel, rootfs, initrd, bios, pflash;
  if (kernel_fd.has_value()) {
    // User-chosen kernel is untrusted.
    is_trusted_image = false;

    int raw_fd = kernel_fd.value().get();
    failure_reason = internal::RemoveCloseOnExec(raw_fd);
    if (!failure_reason.empty())
      return {};
    kernel = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(raw_fd));
  } else {
    kernel = base::FilePath(vm.kernel());
  }

  if (rootfs_fd.has_value()) {
    // User-chosen rootfs is untrusted.
    is_trusted_image = false;

    int raw_fd = rootfs_fd.value().get();
    failure_reason = internal::RemoveCloseOnExec(raw_fd);
    if (!failure_reason.empty())
      return {};
    rootfs = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(raw_fd));
  } else {
    rootfs = base::FilePath(vm.rootfs());
  }

  if (initrd_fd.has_value()) {
    // User-chosen initrd is untrusted.
    is_trusted_image = false;

    int raw_fd = initrd_fd.value().get();
    failure_reason = internal::RemoveCloseOnExec(raw_fd);
    if (!failure_reason.empty())
      return {};
    initrd = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(raw_fd));
  } else {
    initrd = base::FilePath(vm.initrd());
  }

  if (bios_fd.has_value()) {
    // User-chosen bios is untrusted.
    is_trusted_image = false;

    int raw_fd = bios_fd.value().get();
    failure_reason = internal::RemoveCloseOnExec(raw_fd);
    if (!failure_reason.empty())
      return {};
    bios = base::FilePath(kProcFileDescriptorsPath)
               .Append(base::NumberToString(raw_fd));
  } else if (biosDlcPath.has_value() && !biosDlcPath->empty()) {
    bios = biosDlcPath.value();
    bios = bios.Append(kBruschettaBiosDlcPath);
  }

  if (pflash_fd.has_value()) {
    // User-chosen pflash is untrusted.
    is_trusted_image = false;

    int raw_fd = pflash_fd.value().get();
    failure_reason = internal::RemoveCloseOnExec(raw_fd);
    if (!failure_reason.empty())
      return {};
    pflash = base::FilePath(kProcFileDescriptorsPath)
                 .Append(base::NumberToString(raw_fd));
  }

  base::FilePath vm_path;
  // As a legacy fallback, use the component rather than the DLC.
  //
  // TODO(crbug/953544): remove this once we no longer distribute termina as a
  // component.
  if (vm.dlc_id().empty() && is_termina) {
    vm_path = internal::GetLatestVMPath(base::FilePath(kVmDefaultPath));
    if (vm_path.empty()) {
      failure_reason = "Termina component is not loaded";
      return {};
    }
  } else if (vmDlcPath.has_value()) {
    vm_path = vmDlcPath.value();
  }

  // Pull in the DLC-provided files if requested.
  if (!kernel_fd.has_value() && !vm_path.empty())
    kernel = vm_path.Append(kVmKernelName);
  if (!rootfs_fd.has_value() && !vm_path.empty())
    rootfs = vm_path.Append(kVmRootfsName);

  base::FilePath tools_disk;
  if (toolsDlcPath.has_value()) {
    base::FilePath tools_disk_path = toolsDlcPath.value();
    tools_disk = tools_disk_path.Append(kVmToolsDiskName);
  }
  if (tools_disk.empty() && !vm_path.empty())
    tools_disk = vm_path.Append(kVmToolsDiskName);

  return VMImageSpec{
      .kernel = std::move(kernel),
      .initrd = std::move(initrd),
      .rootfs = std::move(rootfs),
      .bios = std::move(bios),
      .pflash = std::move(pflash),
      .tools_disk = std::move(tools_disk),
      .is_trusted_image = is_trusted_image,
  };
}

std::string RemoveCloseOnExec(int raw_fd) {
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

base::FilePath GetLatestVMPath(base::FilePath component_dir) {
  base::FileEnumerator dir_enum(component_dir, false,
                                base::FileEnumerator::DIRECTORIES);
  base::Version latest_version("0");
  base::FilePath latest_path;
  for (base::FilePath path = dir_enum.Next(); !path.empty();
       path = dir_enum.Next()) {
    base::Version version(path.BaseName().value());
    if (!version.IsValid())
      continue;
    if (version > latest_version) {
      latest_version = version;
      latest_path = path;
    }
  }
  return latest_path;
}

}  // namespace internal
}  // namespace concierge
}  // namespace vm_tools
