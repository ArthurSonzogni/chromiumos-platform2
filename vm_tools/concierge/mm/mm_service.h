// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_
#define VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_

#include <string>

#include <base/containers/fixed_flat_set.h>
#include <base/files/scoped_file.h>

#include "vm_tools/common/vm_id.h"

namespace vm_tools::concierge::mm {

using ManagedVmsSet = base::fixed_flat_set<apps::VmType, 1>;

// The MmService owns and manages all components needed for
// virtio-balloon management of client VMs.
class MmService {
 public:
  // Returns whether the specified VM type is managed by the MmService.
  static constexpr const ManagedVmsSet& ManagedVms() { return kManagedVms; }

  // Starts the VM Memory Management Service. Returns true on success, false
  // otherwise.
  bool Start();

  // Called to notify the service that a new VM has started.
  void NotifyVmStarted(apps::VmType type,
                       int vm_cid,
                       const std::string& socket);

  // Called to notify the service that a VM will stop soon.
  void NotifyVmStopping(int vm_cid);

  // Returns an open FD to the kills server.
  base::ScopedFD GetKillsServerConnection(uint32_t read_timeout_ms);

 private:
  // Currently only ARCVM is supported by the MmService.
  static constexpr ManagedVmsSet kManagedVms =
      base::MakeFixedFlatSet<apps::VmType, 1>({apps::VmType::ARCVM});
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_
