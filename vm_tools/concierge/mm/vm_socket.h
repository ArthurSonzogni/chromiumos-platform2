// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_
#define VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/sequence_checker.h>

#include <vm_memory_management/vm_memory_management.pb.h>

using vm_tools::vm_memory_management::VmMemoryManagementPacket;

namespace vm_tools::concierge::mm {

// Handles socket communication for the VmMemoryManagement system.
class VmSocket {
 public:
  VmSocket();

  explicit VmSocket(base::ScopedFD fd);

  virtual ~VmSocket();

  // Returns true iff the socket's fd is valid.
  virtual bool IsValid();

  // Listens on |port| with |backlog_size|. Returns true iff listening was
  // successful.
  virtual bool Listen(int port, size_t backlog_size);

  // Connects the socket to |port|.
  // Returns true iff the connection succeeded.
  virtual bool Connect(int port);

  // Accepts an incoming connection and returns the new fd. |connected_cid| is
  // be set to the origin cid of the incoming connection.
  virtual base::ScopedFD Accept(int& connected_cid);

  // Registers |callback| to be run when the socket is readable. Returns true
  // iff watching the socket is successful.
  virtual bool OnReadable(const base::RepeatingClosure& callback);

  // Reads a packet from the socket. Returns true iff the read was successful.
  virtual bool ReadPacket(VmMemoryManagementPacket& packet);

  // Writes a packet to the socket. Reteurns true iff the write was successful.
  virtual bool WritePacket(const VmMemoryManagementPacket& packet);

  // Releases the owned socket FD.
  base::ScopedFD Release();

 private:
  // Inititalizes the owned fd as a socket.
  bool InitFd();

  // Ensure calls are made on the correct sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  base::ScopedFD fd_ GUARDED_BY_CONTEXT(sequence_checker_);

  std::unique_ptr<base::FileDescriptorWatcher::Controller> fd_watcher_
      GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_VM_SOCKET_H_
