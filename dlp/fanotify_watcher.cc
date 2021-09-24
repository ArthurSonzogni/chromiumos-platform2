// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/fanotify_watcher.h"

#include <fcntl.h>
#include <sys/fanotify.h>
#include <utility>

#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_task_runner_handle.h"
#include "dlp/fanotify_reader_thread.h"

namespace dlp {

FanotifyWatcher::FanotifyWatcher(Delegate* delegate)
    : task_runner_(base::SequencedTaskRunnerHandle::Get()),
      thread_(task_runner_, this),
      delegate_(delegate) {
  DCHECK(delegate);
  fanotify_fd_.reset(fanotify_init(FAN_CLOEXEC | FAN_CLASS_CONTENT,
                                   O_RDONLY | O_CLOEXEC | O_LARGEFILE));
  PCHECK(fanotify_fd_.is_valid()) << "fanotify_init() failed";

  thread_.StartThread(fanotify_fd_.get());
}

FanotifyWatcher::~FanotifyWatcher() = default;

void FanotifyWatcher::AddWatch(const base::FilePath& path) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  int res = fanotify_mark(fanotify_fd_.get(), FAN_MARK_ADD | FAN_MARK_MOUNT,
                          FAN_OPEN_PERM, AT_FDCWD, path.value().c_str());

  if (res != 0) {
    PLOG(ERROR) << "fanotify_mark (" << path << ") failed";
  }
}

void FanotifyWatcher::OnFileOpenRequested(ino_t inode,
                                          int pid,
                                          base::ScopedFD fd) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK(fanotify_fd_.is_valid());

  delegate_->ProcessFileOpenRequest(
      inode, pid,
      base::BindOnce(&FanotifyWatcher::OnRequestProcessed,
                     base::Unretained(this), std::move(fd)));
}

void FanotifyWatcher::OnRequestProcessed(base::ScopedFD fd, bool allowed) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  struct fanotify_response response = {};
  response.fd = fd.get();
  response.response = allowed ? FAN_ALLOW : FAN_DENY;
  HANDLE_EINTR(write(fanotify_fd_.get(), &response, sizeof(response)));
}

}  // namespace dlp
