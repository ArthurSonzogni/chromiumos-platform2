// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/adbd/fd_splice.h"

#include <unistd.h>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace adbd {

FdSpliceThreadBase::FdSpliceThreadBase(const std::string& name,
                                       const int in_fd,
                                       const int out_fd,
                                       const int stop_fd)
    : thread_(name), in_fd_(in_fd), out_fd_(out_fd), stop_fd_(stop_fd) {
  DCHECK_GE(in_fd_, 0);
  DCHECK_GE(out_fd_, 0);
}

FdSpliceThreadBase::~FdSpliceThreadBase() {}

bool FdSpliceThreadBase::Start() {
  if (!thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << thread_.thread_name() << ": Failed to start thread";
    return false;
  }
  if (!thread_.task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&FdSpliceThreadBase::Run, base::Unretained(this)))) {
    LOG(ERROR) << thread_.thread_name()
               << ": Failed to dispatch task to thread ";
    return false;
  }
  // Set up epoll for read/write.
  SetupEpoll();

  VLOG(1) << thread_.thread_name() << " started";
  return true;
}

void FdSpliceThreadBase::Stop() {
  thread_.Stop();
}

// Set up epoll for fd_in/fd_out.
bool FdSpliceThreadBase::SetupEpoll() {
  // Create epoll file descriptors.
  rd_epoll_fd_ = base::ScopedFD(epoll_create1(EPOLL_CLOEXEC));
  if (rd_epoll_fd_.get() < 0) {
    PLOG(ERROR) << thread_.thread_name() << ": epoll_create failed";
    return false;
  }

  wr_epoll_fd_ = base::ScopedFD(epoll_create1(EPOLL_CLOEXEC));
  if (wr_epoll_fd_.get() < 0) {
    PLOG(ERROR) << thread_.thread_name() << ": epoll_create failed";
    return false;
  }

  // Create epoll events.
  struct epoll_event rd_ev {
    .events = EPOLLIN, .data.fd = in_fd_,
  };
  struct epoll_event wr_ev {
    .events = EPOLLOUT, .data.fd = out_fd_,
  };
  struct epoll_event stop_ev {
    .events = EPOLLIN, .data.fd = stop_fd_,
  };

  rd_epoll_events_.push_back(rd_ev);
  wr_epoll_events_.push_back(wr_ev);

  if (stop_fd_ != -1) {
    rd_epoll_events_.push_back(stop_ev);
    wr_epoll_events_.push_back(stop_ev);
  }

  for (struct epoll_event ep : rd_epoll_events_) {
    if (epoll_ctl(rd_epoll_fd_.get(), EPOLL_CTL_ADD, ep.data.fd, &ep) < 0) {
      PLOG(ERROR) << thread_.thread_name()
                  << ": epoll_ctl failed for rd events";
      return false;
    }
  }

  for (struct epoll_event ep : wr_epoll_events_) {
    if (epoll_ctl(wr_epoll_fd_.get(), EPOLL_CTL_ADD, ep.data.fd, &ep) < 0) {
      PLOG(ERROR) << thread_.thread_name()
                  << ": epoll_ctl failed for wr events";
      return false;
    }
  }
  return true;
}

// Read up to num_bytes from in_fd_ into buffer.
ssize_t FdSpliceThreadBase::ReadOnce(char* buffer, size_t num_bytes) {
  size_t max_events = rd_epoll_events_.size();
  struct epoll_event events[max_events];
  ssize_t ret = -1;

  int num_fds =
      HANDLE_EINTR(epoll_wait(rd_epoll_fd_.get(), events, max_events, -1));
  if (num_fds == -1) {
    PLOG(ERROR) << thread_.thread_name() << ": epoll_wait failed for read";
    return ret;
  }

  for (int i = 0; i < num_fds; i++) {
    int fd = events[i].data.fd;
    if (fd == in_fd_) {
      ret = HANDLE_EINTR(read(fd, buffer, num_bytes));
      if (ret < 0) {
        PLOG(ERROR) << thread_.thread_name()
                    << ": Failed to read from endpoint";
        return ret;
      }
    } else if (fd == stop_fd_) {
      LOG(WARNING) << thread_.thread_name() << ": Received thread stop event";
      return -1;
    } else {
      PLOG(ERROR) << thread_.thread_name()
                  << ": Received an invalid read epoll event";
      return -1;
    }
  }
  return ret;
}

// Read num_bytes from in_fd_ into buffer allowing for partial reads.
bool FdSpliceThreadBase::ReadAll(char* buffer, size_t num_bytes) {
  size_t total_read = 0;
  while (total_read < num_bytes) {
    ssize_t bytes_read = ReadOnce(buffer + total_read, num_bytes - total_read);
    if (bytes_read <= 0)
      break;
    total_read += bytes_read;
  }
  return total_read == num_bytes;
}

// Write num_bytes from buffer to out_fd_.
ssize_t FdSpliceThreadBase::WriteOnce(char* buffer, size_t num_bytes) {
  size_t max_events = wr_epoll_events_.size();
  struct epoll_event events[max_events];
  ssize_t ret = -1;

  int num_fds =
      HANDLE_EINTR(epoll_wait(wr_epoll_fd_.get(), events, max_events, -1));
  if (num_fds == -1) {
    PLOG(ERROR) << thread_.thread_name() << ": epoll_wait failed for write";
    return ret;
  }

  for (int i = 0; i < num_fds; i++) {
    int fd = events[i].data.fd;
    if (fd == out_fd_) {
      ret = HANDLE_EINTR(write(fd, buffer, num_bytes));
      if (ret < 0) {
        PLOG(ERROR) << thread_.thread_name() << ": Failed to write to endpoint";
        return ret;
      }
    } else if (fd == stop_fd_) {
      LOG(WARNING) << thread_.thread_name() << ": Received thread stop event";
      return -1;
    } else {
      PLOG(ERROR) << thread_.thread_name()
                  << ": Received an invalid write epoll event";
      return -1;
    }
  }
  return ret;
}

// Write up to num_bytes from buffer to out_fd_ allowing for partial writes.
bool FdSpliceThreadBase::WriteAll(char* buffer, size_t num_bytes) {
  ssize_t total_written = 0;
  while (total_written < num_bytes) {
    ssize_t bytes_written =
        WriteOnce(buffer + total_written, num_bytes - total_written);
    if (bytes_written < 0) {
      break;
    }
    total_written += bytes_written;
  }
  return total_written == num_bytes;
}

}  // namespace adbd
