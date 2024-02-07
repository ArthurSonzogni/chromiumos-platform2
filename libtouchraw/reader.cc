// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/reader.h"

#include <utility>

#include <base/logging.h>

#include "libtouchraw/touchraw.h"

namespace touchraw {

// HID buffer maximum size 16kb. Be consistent with HID_MAX_BUFFER_SIZE defined
// in linux hid.h.
constexpr ssize_t kHIDMaxSize = 16384;
// Report id is the first byte of a HID report.
constexpr int kHIDReportIDIndex = 0;

Reader::Reader(base::ScopedFD fd,
               std::unique_ptr<HIDDataConsumerInterface> q,
               std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher)
    : fd_(std::move(fd)), q_(std::move(q)), watcher_(std::move(watcher)) {}

absl::Status Reader::Start() {
  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    return absl::FailedPreconditionError(
        "sequenced task runner: default queue for the current thread is not "
        "present");
  }

  if (!watcher_) {
    watcher_ = base::FileDescriptorWatcher::WatchReadable(
        fd_.get(), base::BindRepeating(&Reader::OnFileCanReadWithoutBlocking,
                                       base::Unretained(this), fd_.get()));

    if (!watcher_)
      return absl::UnavailableError(
          "failed to create a file descriptor watcher");
  }

  LOG(INFO) << "Start watching device " << fd_.get();
  return absl::OkStatus();
}

void Reader::Stop() {
  LOG(INFO) << "Stop watching device " << fd_.get();
  watcher_.reset();
}

void Reader::OnFileCanReadWithoutBlocking(int fd) {
  if (fd != fd_.get()) {
    LOG(ERROR) << "File descriptor does not match";
    return;
  }

  uint8_t buf[kHIDMaxSize] = {0};
  int attempts = 3;
  ssize_t read_size;
  while (attempts-- > 0) {
    read_size = read(fd, buf, kHIDMaxSize);
    if (read_size < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      if (errno != ENODEV)
        LOG(ERROR) << "Error reading fd " << fd << ": " << strerror(errno);
      Stop();
      return;
    } else {
      break;
    }
  }

  ProcessData(buf, read_size);
}

void Reader::ProcessData(const uint8_t* buf, const ssize_t read_size) {
  auto hid_data = std::make_unique<HIDData>();

  if (!buf || read_size == 0) {
    LOG(WARNING) << "Invalid buffer or read size is zero";
    return;
  }

  // Push HIDData into consumer queue.
  hid_data->report_id = buf[kHIDReportIDIndex];
  hid_data->payload.assign(buf + 1, buf + read_size);

  // Dispatch.
  q_->Push(std::move(hid_data));
}

}  // namespace touchraw
