// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/fanotify_reader_thread.h"

#include <fcntl.h>
#include <sys/fanotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace dlp {

FanotifyReaderThread::FanotifyReaderThread(
    scoped_refptr<base::SequencedTaskRunner> parent_task_runner,
    Delegate* delegate)
    : parent_task_runner_(std::move(parent_task_runner)), delegate_(delegate) {
  CHECK(delegate_);
  CHECK(parent_task_runner_->RunsTasksInCurrentSequence());
}

FanotifyReaderThread::~FanotifyReaderThread() {
  base::PlatformThread::Join(handle_);
}

void FanotifyReaderThread::StartThread(int fanotify_fd) {
  CHECK(parent_task_runner_->RunsTasksInCurrentSequence());
  fanotify_fd_ = fanotify_fd;

  CHECK(base::PlatformThread::Create(0, this, &handle_));
}

void FanotifyReaderThread::ThreadMain() {
  CHECK(!parent_task_runner_->RunsTasksInCurrentSequence());
  base::PlatformThread::SetName("fanotify_reader");

  RunLoop();

  // TODO(poromov): Gracefully stop the thread and notify.
}

void FanotifyReaderThread::RunLoop() {
  CHECK(!parent_task_runner_->RunsTasksInCurrentSequence());

  CHECK_LE(0, fanotify_fd_);
  CHECK_GT(FD_SETSIZE, fanotify_fd_);

  std::vector<char> buffer(0);
  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fanotify_fd_, &rfds);
    // Re-check file descriptor every second.
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    // Wait until some inotify events are available.
    int select_result =
        HANDLE_EINTR(select(fanotify_fd_ + 1, &rfds, nullptr, nullptr, &tv));
    if (select_result < 0) {
      PLOG(WARNING) << "select failed";
      return;
    } else if (select_result == 0) {
      continue;
    }

    // Adjust buffer size to current event queue size.
    int buffer_size;
    int ioctl_result =
        HANDLE_EINTR(ioctl(fanotify_fd_, FIONREAD, &buffer_size));
    if (ioctl_result != 0) {
      PLOG(WARNING) << "ioctl failed";
      return;
    }

    buffer.resize(buffer_size);
    ssize_t bytes_read =
        HANDLE_EINTR(read(fanotify_fd_, &buffer[0], buffer_size));
    if (bytes_read < 0) {
      PLOG(WARNING) << "read from fanotify fd failed";
      return;
    }

    fanotify_event_metadata* metadata =
        reinterpret_cast<fanotify_event_metadata*>(&buffer[0]);
    while (FAN_EVENT_OK(metadata, bytes_read)) {
      if (metadata->vers != FANOTIFY_METADATA_VERSION) {
        LOG(ERROR) << "mismatch of fanotify metadata version";
        return;
      }
      if (metadata->fd >= 0) {
        base::ScopedFD fd(metadata->fd);
        if (metadata->mask & FAN_OPEN_PERM) {
          struct stat st;
          if (fstat(fd.get(), &st)) {
            PLOG(ERROR) << "fstat failed";
            metadata = FAN_EVENT_NEXT(metadata, bytes_read);
            continue;
          }

          parent_task_runner_->PostTask(
              FROM_HERE, base::BindOnce(&Delegate::OnFileOpenRequested,
                                        base::Unretained(delegate_), st.st_ino,
                                        metadata->pid, std::move(fd)));
        } else {
          LOG(WARNING) << "unexpected fanotify event: " << metadata->mask;
        }
      }
      metadata = FAN_EVENT_NEXT(metadata, bytes_read);
    }
  }
}

}  // namespace dlp
