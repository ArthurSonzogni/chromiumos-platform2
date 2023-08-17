// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_start_vm_helper.h"

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
      request.vm().dlc_id() == kBruschettaBiosDlcId)
    return apps::VmType::BRUSCHETTA;
  return apps::VmType::UNKNOWN;
}

}  // namespace internal
}  // namespace concierge
}  // namespace vm_tools
