// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains helper function for startVM or startArcVM service handler

#ifndef VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_
#define VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_

#include <base/check.h>
#include <base/logging.h>
#include <base/system/sys_info.h>
#include <sys/types.h>

#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

// The Id of the DLC that supplies the Bios for the Bruschetta VM.
constexpr char kBruschettaBiosDlcId[] = "edk2-ovmf-dlc";

// The Id of the DLC that supplies the Bios for the Borealis VM.
constexpr char kBorealisBiosDlcId[] = "borealis-dlc";

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
  auto iter = FindVm(request.owner_id(), request.name());
  if (iter != vms_.end()) {
    LOG(INFO) << "VM with requested name is already running";

    VmBaseImpl::Info vm = iter->second->GetInfo();

    VmInfo* vm_info = response->mutable_vm_info();
    vm_info->set_ipv4_address(vm.ipv4_address);
    vm_info->set_pid(vm.pid);
    vm_info->set_cid(vm.cid);
    vm_info->set_seneschal_server_handle(vm.seneschal_server_handle);
    vm_info->set_vm_type(ToLegacyVmType(vm.type));
    switch (vm.status) {
      case VmBaseImpl::Status::STARTING: {
        response->set_status(VM_STATUS_STARTING);
        break;
      }
      case VmBaseImpl::Status::RUNNING: {
        response->set_status(VM_STATUS_RUNNING);
        break;
      }
      default: {
        response->set_status(VM_STATUS_UNKNOWN);
        break;
      }
    }
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
}  // namespace internal

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SERVICE_START_VM_HELPER_H_
