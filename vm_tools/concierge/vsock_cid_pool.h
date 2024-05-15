// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VSOCK_CID_POOL_H_
#define VM_TOOLS_CONCIERGE_VSOCK_CID_POOL_H_

#include <stdint.h>

namespace vm_tools::concierge {

// Manages a pool of virtual socket context IDs to be assigned to VMs.
class VsockCidPool {
 public:
  VsockCidPool() = default;
  VsockCidPool(const VsockCidPool&) = delete;
  VsockCidPool& operator=(const VsockCidPool&) = delete;

  ~VsockCidPool() = default;

  // Allocates and returns a vsock context id.
  uint32_t Allocate() { return next_cid_++; }

 private:
  // The next context id to hand out. Cids 0, 1 and U32_MAX are reserved while
  // cid 2 is always the host system.  Guest cids start at 3. Reserve cids 3-31
  // for static vms.
  uint32_t next_cid_{32};
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VSOCK_CID_POOL_H_
