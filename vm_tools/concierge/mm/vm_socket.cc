// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/vm_socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>

// Needs to be included after sys/socket.h.
#include <linux/vm_sockets.h>

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace vm_tools::concierge::mm {
namespace {

// The maximum allowed size of a VmMemoryManagementPacket.
constexpr uint32_t kPacketMaxSize = 4096;

struct VmMemoryManagementPacketWireFormat {
  // The size of the data of the packet.
  uint32_t data_size;
  // The actual data of the packet.
  uint8_t data[kPacketMaxSize];

  size_t GetTotalWriteSize() { return sizeof(data_size) + data_size; }
};

}  // namespace

VmSocket::VmSocket() = default;

VmSocket::VmSocket(base::ScopedFD fd) : fd_(std::move(fd)) {}

VmSocket::~VmSocket() = default;

bool VmSocket::IsValid() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return fd_.is_valid();
}

bool VmSocket::Listen(int port, size_t backlog_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!InitFd()) {
    return false;
  }

  struct sockaddr_vm sa {};
  sa.svm_family = AF_VSOCK;
  sa.svm_cid = VMADDR_CID_ANY;
  sa.svm_port = port;

  if (bind(fd_.get(), reinterpret_cast<const struct sockaddr*>(&sa),
           sizeof(sa)) == -1) {
    PLOG(ERROR) << "Failed to bind VSOCK.";
    return false;
  }

  if (listen(fd_.get(), backlog_size) == -1) {
    PLOG(ERROR) << "Failed to start listening for a connection on VSOCK.";
    return false;
  }

  return true;
}

bool VmSocket::Connect(int port) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!InitFd()) {
    return false;
  }

  struct sockaddr_vm sa {};
  sa.svm_family = AF_VSOCK;
  sa.svm_port = port;
  sa.svm_cid = VMADDR_CID_LOCAL;

  if (HANDLE_EINTR(connect(fd_.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    PLOG(ERROR) << "Failed to connect to vsock port " << port;
    fd_.reset();
    return false;
  }

  return true;
}

base::ScopedFD VmSocket::Accept(int& connected_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  struct sockaddr_vm client_addr {};
  socklen_t client_addr_len = sizeof(client_addr);

  base::ScopedFD connection;
  connection.reset(HANDLE_EINTR(accept(
      fd_.get(), reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len)));

  if (!connection.is_valid()) {
    PLOG(ERROR) << "Failed accept().";
    return {};
  }

  if (client_addr_len != sizeof(client_addr)) {
    PLOG(ERROR) << "Accept received invalid sock size.";
    return {};
  }

  connected_cid = client_addr.svm_cid;

  return connection;
}

bool VmSocket::OnReadable(const base::RepeatingClosure& callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(fd_.get(), callback);

  if (!fd_watcher_) {
    PLOG(ERROR) << "Failed to start watching VSOCK fd.";
    return false;
  }

  return true;
}

bool VmSocket::ReadPacket(VmMemoryManagementPacket& packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VmMemoryManagementPacketWireFormat wire_packet;

  if (!base::ReadFromFD(fd_.get(),
                        reinterpret_cast<char*>(&wire_packet.data_size),
                        sizeof(wire_packet.data_size))) {
    PLOG(ERROR) << "Failed to read packet size.";
    return false;
  }

  if (wire_packet.data_size > kPacketMaxSize) {
    return false;
  }

  if (!base::ReadFromFD(fd_.get(), reinterpret_cast<char*>(wire_packet.data),
                        wire_packet.data_size)) {
    PLOG(ERROR) << "Failed to read packet data.";
    return false;
  }

  return packet.ParseFromArray(wire_packet.data, wire_packet.data_size);
}

bool VmSocket::WritePacket(const VmMemoryManagementPacket& packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VmMemoryManagementPacketWireFormat wire_packet;

  wire_packet.data_size = packet.ByteSizeLong();

  if (wire_packet.data_size > kPacketMaxSize) {
    return false;
  }

  if (!packet.SerializeToArray(wire_packet.data, wire_packet.data_size)) {
    PLOG(ERROR) << "Failed to serialize packet.";
    return false;
  }

  return base::WriteFileDescriptor(fd_.get(),
                                   {reinterpret_cast<uint8_t*>(&wire_packet),
                                    wire_packet.GetTotalWriteSize()});
}

base::ScopedFD VmSocket::Release() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::move(fd_);
}

bool VmSocket::InitFd() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  fd_.reset(socket(AF_VSOCK, SOCK_STREAM, 0));

  if (!fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create VSOCK.";
    return false;
  }

  return true;
}

}  // namespace vm_tools::concierge::mm
