// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_

namespace vm_tools::concierge::mm {

// The KillsServer accepts and handles low memory kill related
// messages for the VM Memory Management Service.
class KillsServer {
 public:
  explicit KillsServer(int port);

  KillsServer(const KillsServer&) = delete;
  KillsServer& operator=(const KillsServer&) = delete;

  bool StartListening();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_
