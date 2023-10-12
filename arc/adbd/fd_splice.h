// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_ADBD_FD_SPLICE_H_
#define ARC_ADBD_FD_SPLICE_H_

#include <sys/epoll.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/threading/thread.h>

namespace adbd {

// Thread Base class to implement fd splice functionality. Transfers
// data from the file descriptor fd_in to the file descriptor fd_out.
// A stop eventfd is used to interrupt and return from epoll_wait.
class FdSpliceThreadBase {
 public:
  FdSpliceThreadBase(const std::string& name,
                     const int in_fd,
                     const int out_fd,
                     const int stop_fd = -1);
  // Start/Stop the thread.
  bool Start();
  void Stop();

 protected:
  virtual ~FdSpliceThreadBase();
  // Set up epoll file descriptors for read/write operations.
  bool SetupEpoll();
  // Read up to a max of num_bytes into buffer.
  ssize_t ReadOnce(char* buffer, size_t num_bytes);
  // Read exactly num_bytes into buffer.
  bool ReadAll(char* buffer, size_t num_bytes);
  // Write num_bytes into buffer.
  ssize_t WriteOnce(char* buffer, size_t num_bytes);
  // Write num_bytes into buffer allowing partial writes.
  bool WriteAll(char* buffer, size_t num_bytes);
  // Overload to implement splice functionality.
  virtual void Run() = 0;

 private:
  base::Thread thread_;
  const int in_fd_;
  const int out_fd_;
  const int stop_fd_;
  // Epoll file descriptors.
  base::ScopedFD rd_epoll_fd_;
  base::ScopedFD wr_epoll_fd_;
  // Epoll events for in and out fds.
  std::vector<struct epoll_event> rd_epoll_events_;
  std::vector<struct epoll_event> wr_epoll_events_;
};

}  // namespace adbd
#endif  // ARC_ADBD_FD_SPLICE_H_
