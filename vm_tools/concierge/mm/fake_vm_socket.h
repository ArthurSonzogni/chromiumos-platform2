// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_VM_SOCKET_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_VM_SOCKET_H_

#include "vm_tools/concierge/mm/vm_socket.h"

#include <unordered_set>

namespace vm_tools::concierge::mm {

class FakeVmSocket : public VmSocket {
 public:
  FakeVmSocket();
  ~FakeVmSocket();

  bool IsValid() override;
  bool Listen(int port, size_t backlog_size) override;
  bool Connect(int port) override;
  base::ScopedFD Accept(int& connected_cid) override;
  bool OnReadable(const base::RepeatingClosure& callback) override;
  bool ReadPacket(VmMemoryManagementPacket& packet) override;
  bool WritePacket(const VmMemoryManagementPacket& packet) override;

  static size_t instance_count_;

  bool is_valid_ = true;

  size_t listen_call_count_{};
  bool listen_result_ = true;
  int listen_port_{};
  size_t listen_backlog_size_{};

  size_t on_readable_call_count_{};
  bool on_readable_result_ = true;
  base::RepeatingClosure on_readable_{};

  int connected_cid_{};
  base::ScopedFD accept_fd_{};

  bool read_result_ = true;
  VmMemoryManagementPacket packet_to_read_{};

  bool write_result_ = true;
  VmMemoryManagementPacket written_packet_{};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_VM_SOCKET_H_
