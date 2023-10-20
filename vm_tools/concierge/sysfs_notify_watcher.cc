// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/sysfs_notify_watcher.h"

#include <poll.h>
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

  // poll() on the fd on the polling thread.
  // Safety note: Unretained(this) is safe since the poll_thread_ lifetime is
  // coupled to the lifetime of this instance.
  poll_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SysfsNotifyWatcher::PollOnThread,
                                base::Unretained(this), fd_));

  return true;
}

void SysfsNotifyWatcher::PollOnThread(int fd) {
  struct pollfd p;
  p.fd = fd;
  p.events = POLLPRI;
  p.revents = 0;

  // Blocking call. This will only return once POLLPRI is set on the fd or an
  // error occurs.
  int ret = HANDLE_EINTR(poll(&p, 1, -1));

  // Report the poll result to the main thread.
  // Safety note: Unretained(this) is safe since the poll_thread_ lifetime
  // (where this function is run) is coupled to the lifetime of this instance.
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SysfsNotifyWatcher::PollEvent,
                                base::Unretained(this), ret > 0));
}

void SysfsNotifyWatcher::PollEvent(bool success) {
  callback_.Run(success);

  // After a poll event, poll again
  // Safety note: Unretained(this) is safe since the poll_thread_ lifetime is
  // coupled to the lifetime of this instance.
  poll_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SysfsNotifyWatcher::PollOnThread,
                                base::Unretained(this), fd_));
}

SysfsNotifyWatcher::SysfsNotifyWatcher(int fd,
                                       const SysfsNotifyCallback& callback)
    : fd_(fd), callback_(callback) {}

void SysfsNotifyWatcher::SetCallback(const SysfsNotifyCallback& callback) {
  callback_ = callback;
}

}  // namespace vm_tools::concierge
