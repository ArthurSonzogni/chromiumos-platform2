// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/sysfs_notify_watcher.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <memory>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>

namespace vm_tools::concierge {

std::unique_ptr<SysfsNotifyWatcher> SysfsNotifyWatcher::Create(
    int fd, const SysfsNotifyCallback& callback) {
  std::unique_ptr<SysfsNotifyWatcher> watcher =
      base::WrapUnique(new SysfsNotifyWatcher(fd, callback));

  if (!watcher->StartWatching()) {
    return {};
  }

  return watcher;
}

SysfsNotifyWatcher::SysfsNotifyWatcher(int fd,
                                       const SysfsNotifyCallback& callback)
    : fd_(fd), callback_(callback) {}

SysfsNotifyWatcher::~SysfsNotifyWatcher() {
  // Write to the exit fd to signal the poll_thread_ to exit.
  uint64_t data = 1;
  (void)write(exit_fd_.get(), &data, sizeof(data));
}

void SysfsNotifyWatcher::SetCallback(const SysfsNotifyCallback& callback) {
  callback_ = callback;
}

bool SysfsNotifyWatcher::StartWatching() {
  // Since poll is a blocking call spawn a separate thread that will perform the
  // poll and wait until it returns. The poll event will be sent to the main
  // thread when it happens.
  if (!poll_thread_.StartWithOptions(base::Thread::Options(
          base::MessagePumpType::IO,
          0 /* stack_size: 0 corresponds to the default size*/))) {
    LOG(ERROR) << "Failed to start sysfs notify watch thread";
    return false;
  }

  exit_fd_.reset(eventfd(0, 0));

  if (!exit_fd_.is_valid()) {
    LOG(ERROR) << "Failed to create exit fd.";
    return false;
  }

  PollHandler(PollStatus::INIT);

  return true;
}

// static
SysfsNotifyWatcher::PollStatus SysfsNotifyWatcher::PollOnce(int pollpri_fd,
                                                            int exit_fd) {
  struct pollfd p[2] = {{.fd = pollpri_fd, .events = POLLPRI, .revents = 0},
                        {.fd = exit_fd, .events = POLLIN, .revents = 0}};

  // Blocking call. This will only return once POLLPRI is set on |pollpri_fd| or
  // if POLLIN is set on |exit_fd|.
  int ret = HANDLE_EINTR(poll(p, 2, -1));

  // Signaled to exit.
  if (p[1].revents & POLLIN) {
    return PollStatus::EXIT;
  }

  return ret > 0 ? PRI : FAIL;
}

void SysfsNotifyWatcher::PollHandler(PollStatus status) {
  if (status == PollStatus::PRI || status == PollStatus::FAIL) {
    callback_.Run(status == PollStatus::PRI);
  }

  // After a poll event, poll again if not exiting.
  if (status != PollStatus::EXIT) {
    poll_thread_.task_runner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&PollOnce, fd_, exit_fd_.get()),
        base::BindOnce(&SysfsNotifyWatcher::PollHandler,
                       base::Unretained(this)));
  }
}

}  // namespace vm_tools::concierge
