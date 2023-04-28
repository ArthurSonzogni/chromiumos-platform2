// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SHARED_DATA_H_
#define VM_TOOLS_CONCIERGE_SHARED_DATA_H_

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/system/sys_info.h>

#include "vm_tools/concierge/service.h"

namespace vm_tools {
namespace concierge {

// Maximum number of extra disks to be mounted inside the VM.
constexpr int kMaxExtraDisks = 10;

// Cryptohome root base path.
constexpr char kCryptohomeRoot[] = "/run/daemon-store";

// crosvm directory name.
constexpr char kCrosvmDir[] = "crosvm";

// Plugin VM directory name.
constexpr char kPluginVmDir[] = "pvm";

// Path to the runtime directory used by VMs.
constexpr char kRuntimeDir[] = "/run/vm";

// Only allow hex digits in the cryptohome id.
constexpr char kValidCryptoHomeCharacters[] = "abcdefABCDEF0123456789";

// File extension for pflash files.
constexpr char kPflashImageExtension[] = ".pflash";

// Information about the Pflash file associated with a VM.
struct PflashMetadata {
  // Path where pflash should be installed.
  base::FilePath path;

  // Does |path| exist.
  bool is_installed;
};

// Gets the path to the file given the name, user id, location, and extension.
std::optional<base::FilePath> GetFilePathFromName(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    StorageLocation storage_location,
    const std::string& extension,
    bool create_parent_dir,
    base::FilePath storage_dir = base::FilePath(kCryptohomeRoot));

bool GetPluginDirectory(const base::FilePath& prefix,
                        const std::string& extension,
                        const std::string& vm_id,
                        bool create,
                        base::FilePath* path_out);

bool GetPluginIsoDirectory(const std::string& vm_id,
                           const std::string& cryptohome_id,
                           bool create,
                           base::FilePath* path_out);

bool IsValidOwnerId(const std::string& owner_id);

bool IsValidVmName(const std::string& vm_name);

void SendDbusResponse(dbus::ExportedObject::ResponseSender response_sender,
                      dbus::MethodCall* method_call,
                      const google::protobuf::MessageLite& response);

// Returns information about the Pflash file associated with a VM. If there is a
// failure in querying the information then it returns std::nullopt.
std::optional<PflashMetadata> GetPflashMetadata(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    base::FilePath storage_dir = base::FilePath(kCryptohomeRoot));

// Returns in order -
// 1. An installed pflash file for the VM.
// 2. A valid |start_vm_request_pflash_path|
// 3. An empty file path.
//
// Returns an error -
// 1. If a pflash file is installed and |start_vm_request_pflash_path| is valid.
// 2. If there is an error in querying information about any installed pflash
// file.
std::optional<base::FilePath> GetInstalledOrRequestPflashPath(
    const VmId& vm_id, const base::FilePath& start_vm_request_pflash_path);

template <class StartXXRequest,
          int64_t (Service::*GetVmMemory)(const StartXXRequest&),
          StartVmResponse (Service::*StartVm)(
              StartXXRequest, std::unique_ptr<dbus::MessageReader>, VmMemoryId)>
void Service::StartVmHelper(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  auto reader = std::make_unique<dbus::MessageReader>(method_call);

  StartXXRequest request;
  StartVmResponse response;
  // We change to a success status later if necessary.
  response.set_status(VM_STATUS_FAILURE);

  if (!reader->PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StartVmRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  if (!IsValidOwnerId(request.owner_id())) {
    LOG(ERROR) << "Empty or malformed owner ID";
    response.set_failure_reason("Empty or malformed owner ID");
    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  if (!IsValidVmName(request.name())) {
    LOG(ERROR) << "Empty or malformed VM name";
    response.set_failure_reason("Empty or malformed VM name");
    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  // Check the CPU count.
  if (request.cpus() > base::SysInfo::NumberOfProcessors()) {
    LOG(ERROR) << "Invalid number of CPUs: " << request.cpus();
    response.set_failure_reason("Invalid CPU count");
    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter != vms_.end()) {
    LOG(INFO) << "VM with requested name is already running";

    VmInterface::Info vm = iter->second->GetInfo();

    VmInfo* vm_info = response.mutable_vm_info();
    vm_info->set_ipv4_address(vm.ipv4_address);
    vm_info->set_pid(vm.pid);
    vm_info->set_cid(vm.cid);
    vm_info->set_seneschal_server_handle(vm.seneschal_server_handle);
    vm_info->set_vm_type(vm.type);
    switch (vm.status) {
      case VmInterface::Status::STARTING: {
        response.set_status(VM_STATUS_STARTING);
        break;
      }
      case VmInterface::Status::RUNNING: {
        response.set_status(VM_STATUS_RUNNING);
        break;
      }
      default: {
        response.set_status(VM_STATUS_UNKNOWN);
        break;
      }
    }
    response.set_success(true);

    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  VmId vm_id(request.owner_id(), request.name());
  auto op_iter = std::find_if(
      disk_image_ops_.begin(), disk_image_ops_.end(), [&vm_id](auto& info) {
        return info.op->vm_id() == vm_id &&
               info.op->status() == DISK_STATUS_IN_PROGRESS;
      });
  if (op_iter != disk_image_ops_.end()) {
    LOG(INFO) << "A disk operation for the VM is in progress";

    response.set_status(VM_STATUS_DISK_OP_IN_PROGRESS);
    response.set_failure_reason("A disk operation for the VM is in progress");
    response.set_success(false);

    SendDbusResponse(std::move(response_sender), method_call, response);
    return;
  }

  response = (this->*StartVm)(std::move(request), std::move(reader),
                              next_vm_memory_id_++);
  SendDbusResponse(std::move(response_sender), method_call, response);
  return;
}

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SHARED_DATA_H_
