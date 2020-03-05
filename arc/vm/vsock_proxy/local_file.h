// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_VSOCK_PROXY_LOCAL_FILE_H_
#define ARC_VM_VSOCK_PROXY_LOCAL_FILE_H_

#include <stdint.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "arc/vm/vsock_proxy/message.pb.h"

namespace arc {

// LocalFile supports writing and reading from a file descriptor owned by this
// proxy process.
class LocalFile {
 public:
  // |can_send_fds| must be true to send/receive FDs using this object.
  // |error_handler| will be run on async IO error.
  // TODO(hashimoto): Change the interface to report all IO errors via
  // |error_handler|, instead of synchronously returning bool.
  LocalFile(base::ScopedFD fd,
            bool can_send_fds,
            base::OnceClosure error_handler);
  ~LocalFile();

  // Reads the message from the file descriptor.
  // Returns a struct of error_code, where it is 0 on succeess or errno, blob
  // and attached fds if available.
  struct ReadResult {
    int error_code;
    std::string blob;
    std::vector<base::ScopedFD> fds;
  };
  ReadResult Read();

  // Writes the given blob and file descriptors to the wrapped file descriptor.
  // Returns true iff the whole message is written.
  bool Write(std::string blob, std::vector<base::ScopedFD> fds);

  // Reads |count| bytes from the file starting at |offset|.
  // Returns whether pread() is supported or not.
  // If supported, the result will be constructed in |response|.
  bool Pread(uint64_t count,
             uint64_t offset,
             arc_proxy::PreadResponse* response);

  // Fills the file descriptor's stat attribute to the |response|.
  // Returns whether fstat(2) is supported or not.
  bool Fstat(arc_proxy::FstatResponse* response);

 private:
  void TrySendMsg();

  base::ScopedFD fd_;
  const bool can_send_fds_;
  base::OnceClosure error_handler_;

  struct Data {
    std::string blob;
    std::vector<base::ScopedFD> fds;
  };
  std::deque<Data> pending_write_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> writable_watcher_;

  base::WeakPtrFactory<LocalFile> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(LocalFile);
};

}  // namespace arc

#endif  // ARC_VM_VSOCK_PROXY_LOCAL_FILE_H_
