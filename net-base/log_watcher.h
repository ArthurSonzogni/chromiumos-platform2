// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_LOG_WATCHER_H_
#define NET_BASE_LOG_WATCHER_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <brillo/brillo_export.h>

namespace net_base {

// Monitors the log's file descriptor, and the callback will be invoked once for
// each line of the logs (separated by the newline character).
// Note: The caller should not destroy the LogWatcher instance in the callback.
class BRILLO_EXPORT LogWatcher {
 public:
  using LogReadyCB = base::RepeatingCallback<void(std::string_view)>;

  // Creates a LogWatcher instance. Returns nullptr if the fd fails to set to
  // non-blocking.
  static std::unique_ptr<LogWatcher> Create(base::ScopedFD log_fd,
                                            LogReadyCB log_ready_cb);

  ~LogWatcher();

 private:
  LogWatcher(base::ScopedFD log_fd, LogReadyCB log_ready_cb);

  void OnLogReady();

  // The log's file descriptor.
  base::ScopedFD log_fd_;

  // The callback when a log is ready.
  LogReadyCB log_ready_cb_;

  // Monitors the file descriptor of the log's fd.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> fd_watcher_;

  std::string stash_token_;
};

}  // namespace net_base

#endif  // NET_BASE_LOG_WATCHER_H_
