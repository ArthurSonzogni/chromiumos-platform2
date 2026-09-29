// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>

#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/service_common.h"
#include "vm_tools/concierge/vmplugin_dispatcher_interface.h"

namespace vm_tools::concierge {

namespace {

bool GetPluginStatefulDirectory(const VmId& vm_id,
                                bool create,
                                base::FilePath* path_out) {
  return GetPluginDirectory(base::FilePath(kCryptohomeRoot)
                                .Append(kPluginVmDir)
                                .Append(vm_id.owner_id()),
                            "pvm", vm_id.name(), create, path_out);
}

}  // namespace

void Service::StartPluginVm(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        vm_tools::concierge::StartVmResponse>> response_cb,
    const vm_tools::concierge::StartPluginVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  LOG(INFO) << "Plugin VM is no longer supported. "
            << "Rejecting StartPluginVm request.";

  StartVmResponse response;
  response.set_success(false);
  response.set_status(VM_STATUS_FAILURE);
  response.set_failure_reason("Plugin VM is no longer supported");

  response_cb->Return(response);
}

bool Service::RenamePluginVm(const VmId& old_id,
                             const VmId& new_id,
                             std::string* failure_reason) {
  base::FilePath old_dir;
  if (!GetPluginStatefulDirectory(old_id, false /* create */, &old_dir)) {
    *failure_reason = "unable to determine current VM directory";
    return false;
  }

  base::FilePath old_iso_dir;
  if (!GetPluginIsoDirectory(old_id, false /* create */, &old_iso_dir)) {
    *failure_reason = "unable to determine current VM ISO directory";
    return false;
  }

  base::FilePath new_dir;
  if (!GetPluginStatefulDirectory(new_id, false /* create */, &new_dir)) {
    *failure_reason = "unable to determine new VM directory";
    return false;
  }

  base::FilePath new_iso_dir;
  if (!GetPluginIsoDirectory(new_id, false /* create */, &new_iso_dir)) {
    *failure_reason = "unable to determine new VM ISO directory";
    return false;
  }

  bool registered;
  if (!pvm::dispatcher::IsVmRegistered(bus_, vmplugin_service_proxy_, old_id,
                                       &registered)) {
    *failure_reason = "failed to check Plugin VM registration status";
    return false;
  }

  // This is unexpected: the VM is not registered. Better leave it alone.
  if (!registered) {
    *failure_reason = "the VM is not registered";
    return false;
  }

  bool is_shut_down;
  if (!pvm::dispatcher::IsVmShutDown(bus_, vmplugin_service_proxy_, old_id,
                                     &is_shut_down)) {
    *failure_reason = "failed to check Plugin VM state";
    return false;
  }

  if (!is_shut_down) {
    *failure_reason = "VM is not shut down";
    return false;
  }

  base::File::Error move_error;
  if (base::PathExists(old_iso_dir) &&
      !base::ReplaceFile(old_iso_dir, new_iso_dir, &move_error)) {
    *failure_reason = std::string("failed to rename VM ISO directory: ") +
                      base::File::ErrorToString(move_error);
    return false;
  }

  if (!pvm::dispatcher::UnregisterVm(bus_, vmplugin_service_proxy_, old_id)) {
    *failure_reason = "failed to temporarily unregister VM";
    return false;
  }

  if (!base::ReplaceFile(old_dir, new_dir, &move_error)) {
    *failure_reason = std::string("failed to rename VM directory: ") +
                      base::File::ErrorToString(move_error);
    return false;
  }

  if (!pvm::dispatcher::RegisterVm(bus_, vmplugin_service_proxy_, new_id,
                                   new_dir)) {
    *failure_reason = "Failed to re-register renamed VM";
    return false;
  }

  return true;
}

}  // namespace vm_tools::concierge
