// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_vm_socket.h"

#include <gtest/gtest.h>

#include <unordered_set>
#include <utility>

namespace vm_tools::concierge::mm {

size_t FakeVmSocket::instance_count_ = 0;

FakeVmSocket::FakeVmSocket() : VmSocket() {
  instance_count_++;
}

FakeVmSocket::~FakeVmSocket() {
  instance_count_--;
}

bool FakeVmSocket::IsValid() {
  return is_valid_;
}

bool FakeVmSocket::Listen(int port, size_t backlog_size) {
  listen_call_count_++;
  listen_port_ = port;
  listen_backlog_size_ = backlog_size;
  return listen_result_;
}

bool FakeVmSocket::Connect(int port) {
  return true;
}

base::ScopedFD FakeVmSocket::Accept(int& connected_cid) {
  connected_cid = connected_cid_;
  return std::move(accept_fd_);
}

bool FakeVmSocket::OnReadable(const base::RepeatingClosure& callback) {
  on_readable_call_count_++;
  on_readable_ = std::move(callback);
  return on_readable_result_;
}

bool FakeVmSocket::ReadPacket(VmMemoryManagementPacket& packet) {
  packet = packet_to_read_;
  return read_result_;
}

bool FakeVmSocket::WritePacket(const VmMemoryManagementPacket& packet) {
  written_packet_ = packet;
  return write_result_;
}

}  // namespace vm_tools::concierge::mm
