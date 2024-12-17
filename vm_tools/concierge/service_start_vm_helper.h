// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains helper function for startVM or startArcVM service handler

#ifndef VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_
#define VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/system/sys_info.h>

#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

// Default path to VM kernel image and rootfs.
constexpr char kVmDefaultPath[] = "/run/imageloader/cros-termina";

// Name of the VM kernel image.
constexpr char kVmKernelName[] = "vm_kernel";

// Name of the VM rootfs image.
constexpr char kVmRootfsName[] = "vm_rootfs.img";

// The Id of the DLC that supplies the Bios for the Bruschetta VM.
constexpr char kBruschettaBiosDlcId[] = "edk2-ovmf-dlc";

// The Id of the DLC that supplies the Bios for the Borealis VM.
constexpr char kBorealisBiosDlcId[] = "borealis-dlc";

// Name of the VM tools image to be mounted at kToolsMountPath.
constexpr char kVmToolsDiskName[] = "vm_tools.img";

// File path for the Bruschetta Bios file inside the DLC root.
constexpr char kBruschettaBiosDlcPath[] = "opt/CROSVM_CODE.fd";

// Socket pair for connecting vhost_user frontend and backend. The frontend
// socket is for crosvm run, and the backend socket is for crosvm device.
struct VhostUserSocketPair {
  base::ScopedFD front_end_fd;
  base::ScopedFD back_end_fd;
};

// Check Cpu setting in request not exceeds processor number
template <class T>
bool CheckCpuCount(const T& request, StartVmResponse* response) {
  // Check the CPU count.
  uint32_t cpus = request.cpus();
  if (cpus > base::SysInfo::NumberOfProcessors()) {
    LOG(ERROR) << "Invalid number of CPUs: " << request.cpus();
    response->set_failure_reason("Invalid CPU count");
    return false;
  }
  return true;
}

template <class T>
bool Service::CheckExistingVm(const T& request, StartVmResponse* response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  VmId vm_id(request.owner_id(), request.name());
  auto iter = FindVm(vm_id);
  if (iter != vms_.end()) {
    LOG(INFO) << "VM with requested name is already running";

    VmBaseImpl::Info vm = iter->second->GetInfo();

    *response->mutable_vm_info() = ToVmInfo(vm, false);
    response->set_status(ToVmStatus(vm.status));
    response->set_success(true);
    return false;
  }
  return true;
}

template <class T>
bool Service::CheckExistingDisk(const T& request, StartVmResponse* response) {
  VmId vm_id(request.owner_id(), request.name());
  auto op_iter = std::find_if(
      disk_image_ops_.begin(), disk_image_ops_.end(), [&vm_id](auto& info) {
        return info.op->vm_id() == vm_id &&
               info.op->status() == DISK_STATUS_IN_PROGRESS;
      });
  if (op_iter != disk_image_ops_.end()) {
    LOG(INFO) << "A disk operation for the VM is in progress";

    response->set_status(VM_STATUS_DISK_OP_IN_PROGRESS);
    response->set_failure_reason("A disk operation for the VM is in progress");
    response->set_success(false);

    return false;
  }
  return true;
}

// Returns false if any preconditions are not met.
template <class StartXXRequest>
bool Service::CheckStartVmPreconditions(const StartXXRequest& request,
                                        StartVmResponse* response) {
  if (!CheckVmNameAndOwner(request, response)) {
    return false;
  }

  if (!CheckCpuCount(request, response)) {
    return false;
  }

  if (!CheckExistingVm(request, response)) {
    return false;
  }

  if (!CheckExistingDisk(request, response)) {
    return false;
  }
  return true;
}

namespace internal {

// Determines what classification type this VM has. Classifications are
// roughly related to products, and the classification broadly determines what
// features are available to a given VM.
apps::VmType ClassifyVm(const StartVmRequest& request);

// Get capacity & cluster & affinity information for cpu0~cpu${cpus}
VmBuilder::VmCpuArgs GetVmCpuArgs(int32_t cpus,
                                  const base::FilePath& cpu_info_path);
// Determines key components of a VM image. Also, decides if it's a trusted
// VM. Returns the empty struct and sets |failure_reason| in the event of a
// failure.
VMImageSpec GetImageSpec(const std::optional<base::ScopedFD>& kernel_fd,
                         const std::optional<base::ScopedFD>& rootfs_fd,
                         const std::optional<base::ScopedFD>& initrd_fd,
                         const std::optional<base::ScopedFD>& bios_fd,
                         const std::optional<base::ScopedFD>& pflash_fd,
                         const std::optional<base::FilePath>& biosDlcPath,
                         const std::optional<base::FilePath>& vmDlcPath,
                         const std::optional<base::FilePath>& toolsDlcPath,
                         std::string& failure_reason);

// Clears close-on-exec flag for a file descriptor to pass it to a subprocess
// such as crosvm. Returns a failure reason on failure.
std::string RemoveCloseOnExec(const base::ScopedFD& fd);

// Create a socket pair for connecting vhost_user frontend and backend.
std::optional<VhostUserSocketPair> SetupVhostUserSocketPair();

}  // namespace internal

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_
