// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/process/process_reaper.h"

#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/location_logging.h>

namespace brillo {

ProcessReaper::~ProcessReaper() {
  Unregister();
}

void ProcessReaper::Register(
    AsynchronousSignalHandlerInterface* async_signal_handler) {
  CHECK(!async_signal_handler_);
  async_signal_handler_ = async_signal_handler;
  async_signal_handler->RegisterHandler(
      SIGCHLD, base::BindRepeating(&ProcessReaper::HandleSIGCHLD,
                                   base::Unretained(this)));
}

void ProcessReaper::Unregister() {
  if (!async_signal_handler_) {
    return;
  }
  async_signal_handler_->UnregisterHandler(SIGCHLD);
  async_signal_handler_ = nullptr;
}

bool ProcessReaper::WatchForChild(const base::Location& from_here,
                                  pid_t pid,
                                  ChildCallback callback) {
  if (watched_processes_.find(pid) != watched_processes_.end()) {
    return false;
  }
  watched_processes_.emplace(pid,
                             WatchedProcess{from_here, std::move(callback)});
  return true;
}

bool ProcessReaper::ForgetChild(pid_t pid) {
  return watched_processes_.erase(pid) != 0;
}

bool ProcessReaper::HandleSIGCHLD(
    const struct signalfd_siginfo& /* sigfd_info */) {
  // One SIGCHLD may correspond to multiple terminated children, so ignore
  // sigfd_info and reap any available children.
  while (true) {
    siginfo_t info;
    info.si_pid = 0;
    int rc = HANDLE_EINTR(waitid(P_ALL, 0, &info, WNOHANG | WEXITED));

    if (rc == -1) {
      if (errno != ECHILD) {
        PLOG(ERROR) << "waitid failed";
      }
      break;
    }

    if (info.si_pid == 0) {
      break;
    }

    auto proc = watched_processes_.find(info.si_pid);
    if (proc == watched_processes_.end()) {
      LOG(INFO) << "Untracked process " << info.si_pid
                << " terminated with status " << info.si_status
                << " (code = " << info.si_code << ")";
    } else {
      DVLOG_LOC(proc->second.location, 1)
          << "Process " << info.si_pid << " terminated with status "
          << info.si_status << " (code = " << info.si_code << ")";
      ChildCallback callback = std::move(proc->second.callback);
      watched_processes_.erase(proc);
      std::move(callback).Run(info);
    }
  }

  // Return false to indicate that our handler should not be uninstalled.
  return false;
}

}  // namespace brillo
