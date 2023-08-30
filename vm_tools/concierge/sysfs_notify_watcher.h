// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SYSFS_NOTIFY_WATCHER_H_
#define VM_TOOLS_CONCIERGE_SYSFS_NOTIFY_WATCHER_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/threading/thread.h>

namespace vm_tools::concierge {

// Watch for high priority data (POLLPRI) on a file and run
// a specified callback when data is available.
// Note: This class reports POLLPRI events with a 'best effort' approach. Not
// all events are guaranteed to be reported, especially if they occur in rapid
// succession.

// Ideally base::FileDescriptorWatcher could be used, but POLLPRI is not
// currently supported by libchrome's message pump infrastructure. Once the
// switch from MessagePumpLibevent to MessagePumpEpoll in libchrome has been
// completed (crbug/1243354), POLLPRI support can be added to libchrome and we
// can switch to using a FileDescriptorWatcher instead.
class SysfsNotifyWatcher {
 public:
  using SysfsNotifyCallback = base::RepeatingCallback<void(const bool)>;

  static std::unique_ptr<SysfsNotifyWatcher> Create(
      int fd, const SysfsNotifyCallback& callback);

  ~SysfsNotifyWatcher();
  SysfsNotifyWatcher(const SysfsNotifyWatcher&) = delete;
  SysfsNotifyWatcher& operator=(const SysfsNotifyWatcher&) = delete;

  void SetCallback(const SysfsNotifyCallback& callback);

 private:
  enum PollStatus { FAIL, INIT, PRI, EXIT };

  static PollStatus PollOnce(int pollpri_fd, int exit_fd);

  SysfsNotifyWatcher(int fd, const SysfsNotifyCallback& callback);

  // Start watching the fd.
  bool StartWatching();

  // Callback that runs when poll() returns.
  void PollHandler(PollStatus status);

  // The specific fd to watch
  const int fd_;

  // The callback that is run after a POLLPRI event on fd
  SysfsNotifyCallback callback_;

  // Used to signal the poll_thread to exit.
  base::ScopedFD exit_fd_;

  // Used to run a poll() in the background.
  base::Thread poll_thread_{"Sysfs_Notify_Poll_Thread"};
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SYSFS_NOTIFY_WATCHER_H_
