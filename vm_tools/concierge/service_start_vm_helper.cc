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

#include "base/files/file_path.h"

namespace vm_tools {
namespace concierge {
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

}  // namespace internal
}  // namespace concierge
}  // namespace vm_tools
