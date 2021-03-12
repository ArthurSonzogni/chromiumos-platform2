// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/pipe_utils.h"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace runtime_probe {

namespace {

enum class PipeState {
  PENDING,
  ERROR,
  DONE,
};

// The system-defined size of buffer used to read from a pipe.
constexpr size_t kBufferSize = PIPE_BUF;

// Seconds to wait for runtime_probe helper to send probe results.
constexpr time_t kWaitSeconds = 5;

PipeState ReadPipe(int src_fd, std::string* dst_str) {
  char buffer[kBufferSize];
  const ssize_t bytes_read = HANDLE_EINTR(read(src_fd, buffer, kBufferSize));
  if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    PLOG(ERROR) << "read() from fd " << src_fd << " failed";
    return PipeState::ERROR;
  }
  if (bytes_read == 0) {
    return PipeState::DONE;
  }
  if (bytes_read > 0) {
    dst_str->append(buffer, bytes_read);
  }
  return PipeState::PENDING;
}

}  // namespace

bool ReadNonblockingPipeToString(int fd, std::string* out) {
  fd_set read_fds;
  struct timeval timeout;

  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);

  timeout.tv_sec = kWaitSeconds;
  timeout.tv_usec = 0;

  while (true) {
    int retval =
        HANDLE_EINTR(select(fd + 1, &read_fds, nullptr, nullptr, &timeout));
    if (retval < 0) {
      PLOG(ERROR) << "select() failed from runtime_probe_helper";
      return false;
    }

    // Should only happen on timeout. Log a warning here, so we get at least a
    // log if the process is stale.
    if (retval == 0) {
      LOG(WARNING) << "select() timed out. Process might be stale.";
      return false;
    }

    PipeState state = ReadPipe(fd, out);
    if (state == PipeState::DONE) {
      return true;
    }
    if (state == PipeState::ERROR) {
      return false;
    }
  }
}

}  // namespace runtime_probe
