// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_

namespace vm_tools::concierge::mm {

// The ReclaimServer accepts and handles MGLRU related
// messages for the VM Memory Management Service.
class ReclaimServer {
 public:
  explicit ReclaimServer(int port);

  ReclaimServer(const ReclaimServer&) = delete;
  ReclaimServer& operator=(const ReclaimServer&) = delete;

  bool StartListening();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_
