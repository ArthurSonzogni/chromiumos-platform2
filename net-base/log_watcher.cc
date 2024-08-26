// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/log_watcher.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_tokenizer.h>

namespace net_base {

std::unique_ptr<LogWatcher> LogWatcher::Create(base::ScopedFD log_fd,
                                               LogReadyCB log_ready_cb) {
  if (!base::SetNonBlocking(log_fd.get())) {
    PLOG(ERROR) << "Failed to set the fd to non-blocking";
    return nullptr;
  }

  return base::WrapUnique(
      new LogWatcher(std::move(log_fd), std::move(log_ready_cb)));
}

LogWatcher::LogWatcher(base::ScopedFD log_fd, LogReadyCB log_ready_cb)
    : log_fd_(std::move(log_fd)), log_ready_cb_(std::move(log_ready_cb)) {
  fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      log_fd_.get(),
      base::BindRepeating(&LogWatcher::OnLogReady,
                          // The callback will not outlive the object.
                          base::Unretained(this)));
}

LogWatcher::~LogWatcher() = default;

void LogWatcher::OnLogReady() {
  static char buf[256];

  while (true) {
    const ssize_t len = read(log_fd_.get(), buf, sizeof(buf));
    if (len <= 0) {
      break;
    }

    // Split to string.
    base::CStringTokenizer tokenizer(buf, buf + len, "\n");
    tokenizer.set_options(base::StringTokenizer::RETURN_DELIMS);
    while (tokenizer.GetNext()) {
      if (tokenizer.token_is_delim()) {
        log_ready_cb_.Run(stash_token_);
        stash_token_ = "";
      } else {
        stash_token_ += tokenizer.token();
      }
    }
  }
}

}  // namespace net_base
