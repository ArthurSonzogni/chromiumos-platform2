// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_
#define VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_

#include <base/files/scoped_file.h>
#include <base/sequence_checker.h>

namespace vm_tools::concierge::mm {

// Handles socket communication for the VmMemoryManagement system.
class VmSocket {
 public:
  VmSocket();

  explicit VmSocket(base::ScopedFD fd);

  // Connects the socket to |port| and sets the read timeout to |timeout_ms|.
  // Returns true iff the connection succeeded.
  bool Connect(int port, int timeout_ms);

  // Releases the owned socket FD.
  base::ScopedFD Release();

 private:
  // Inititalizes the owned fd as a socket.
  bool InitFd();

  // Ensure calls are made on the correct sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  base::ScopedFD fd_ GUARDED_BY_CONTEXT(sequence_checker_){};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_
